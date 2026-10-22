// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "resources/memory/MemoryPool.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <expected>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <utility>

#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryBlock.hpp"
#include "resources/memory/MemoryProperties.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{
namespace
{
constexpr auto hostBlockSize = std::uint64_t {8} * 1024 * 1024;
constexpr auto deviceBlockSize = std::uint64_t {64} * 1024 * 1024;

auto validateRequest(const MemoryRequest& request) -> Result<void>
{
    if (request.requirements.size == 0 || !std::has_single_bit(request.requirements.alignment) ||
        static_cast<bool>(request.resource.buffer) == static_cast<bool>(request.resource.image) ||
        request.resource.pNext != nullptr)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid memory requirements or resource id")};
    }
    return {};
}

auto memoryScore(vk::MemoryPropertyFlags flags, const MemoryRequest& request) -> std::uint32_t
{
    const auto preferred =
        static_cast<std::uint32_t>(std::popcount(static_cast<std::uint32_t>(flags & request.preferredProperties)));
    const auto hostIntent =
        (request.requiredProperties | request.preferredProperties) & vk::MemoryPropertyFlagBits::eHostVisible;
    const auto coherent = hostIntent && (flags & vk::MemoryPropertyFlagBits::eHostCoherent);
    return (preferred * 2U) + (coherent ? 1U : 0U);
}

auto canRetryDedicated(const Error& error) -> bool
{
    return error.code == ErrorCode::ResourceExhausted ||
           (error.native && error.native->api == "Vulkan" &&
            error.native->code == static_cast<std::int64_t>(vk::Result::eErrorMemoryMapFailed));
}
}

auto MemoryPool::create(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device)
    -> std::unique_ptr<MemoryPool>
{
    return std::unique_ptr<MemoryPool> {
        new MemoryPool {physicalDevice, device}
    };
}

MemoryPool::MemoryPool(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device)
    : _physicalDevice {physicalDevice},
      _device {device},
      _memoryProperties {physicalDevice.getMemoryProperties()}
{
    const auto properties =
        physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceMaintenance3Properties>();
    _limits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
    _maximumAllocationSize = properties.get<vk::PhysicalDeviceMaintenance3Properties>().maxMemoryAllocationSize;
    _heapCommitted.resize(_memoryProperties.memoryHeapCount);
}

MemoryPool::~MemoryPool()
{
    expect(_allocations.empty(), "Memory pool destroyed with live allocation owners");
}

auto MemoryPool::selectMemoryType(const MemoryRequest& request) const -> Result<std::uint32_t>
{
    auto selected = std::optional<std::uint32_t> {};
    auto bestScore = std::uint32_t {0};
    for (auto index = std::uint32_t {0}; index < _memoryProperties.memoryTypeCount; ++index)
    {
        const auto flags = _memoryProperties.memoryTypes.at(index).propertyFlags;
        if ((request.requirements.memoryTypeBits & (std::uint32_t {1} << index)) == 0 ||
            (flags & unsupportedMemoryProperties) || (flags & request.requiredProperties) != request.requiredProperties)
        {
            continue;
        }
        const auto score = memoryScore(flags, request);
        if (!selected || score > bestScore)
        {
            selected = index;
            bestScore = score;
        }
    }
    if (!selected)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "No compatible memory type satisfies the request")};
    }
    return *selected;
}

auto MemoryPool::getBlockSize(std::uint32_t memoryType) const -> std::uint64_t
{
    const auto type = _memoryProperties.memoryTypes.at(memoryType);
    const auto nominal =
        (type.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) ? hostBlockSize : deviceBlockSize;
    return std::min({nominal, _memoryProperties.memoryHeaps.at(type.heapIndex).size, _maximumAllocationSize});
}

auto MemoryPool::allocate(const MemoryRequest& request) -> Result<MemoryAllocation>
{
    const auto valid = validateRequest(request);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    if (_nextId == std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Memory allocation identities exhausted")};
    }
    return selectMemoryType(request).and_then([&](std::uint32_t memoryType) -> Result<MemoryAllocation> {
        if (request.dedicated.requiresDedicatedAllocation == vk::True ||
            request.dedicated.prefersDedicatedAllocation == vk::True ||
            request.requirements.size > getBlockSize(memoryType) / 2)
        {
            return makeBlock(request, memoryType, true);
        }
        auto existing = tryExisting(request, memoryType);
        if (existing || existing.error().code != ErrorCode::ResourceExhausted)
        {
            return existing;
        }
        return grow(request, memoryType);
    });
}

auto MemoryPool::tryExisting(const MemoryRequest& request, std::uint32_t memoryType) -> Result<MemoryAllocation>
{
    for (auto& block : _blocks)
    {
        if (block.dedicated || block.memoryType != memoryType ||
            block.image != static_cast<bool>(request.resource.image))
        {
            continue;
        }
        auto range = block.memory->allocate(request.requirements.size, request.requirements.alignment);
        if (range)
        {
            return publish(block, *range);
        }
        if (range.error().code != ErrorCode::ResourceExhausted)
        {
            return std::unexpected {range.error()};
        }
    }
    return std::unexpected {makeError(ErrorCode::ResourceExhausted, "No existing memory block fits")};
}

auto MemoryPool::grow(const MemoryRequest& request, std::uint32_t memoryType) -> Result<MemoryAllocation>
{
    auto ordinary = makeBlock(request, memoryType, false);
    if (ordinary || !canRetryDedicated(ordinary.error()))
    {
        return ordinary;
    }
    return makeBlock(request, memoryType, true);
}

auto MemoryPool::checkBudget(std::uint32_t memoryType, std::uint64_t size) -> Result<void>
{
    const auto heap = _memoryProperties.memoryTypes.at(memoryType).heapIndex;
    const auto heapSize = _memoryProperties.memoryHeaps.at(heap).size;
    if (_stats.allocationCount >= _limits.maxMemoryAllocationCount || size > heapSize - _heapCommitted.at(heap))
    {
        reclaimEmptyBlocks();
    }
    if (size == 0 || size > _maximumAllocationSize || _stats.allocationCount >= _limits.maxMemoryAllocationCount ||
        size > heapSize - _heapCommitted.at(heap) ||
        size > std::numeric_limits<std::uint64_t>::max() - _stats.committedBytes)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Memory allocation count or byte budget exhausted")};
    }
    return {};
}

auto MemoryPool::makeBlock(const MemoryRequest& request, std::uint32_t memoryType, bool dedicated)
    -> Result<MemoryAllocation>
{
    const auto size = dedicated ? request.requirements.size : getBlockSize(memoryType);
    const auto budget = checkBudget(memoryType, size);
    if (!budget)
    {
        return std::unexpected {budget.error()};
    }
    auto memory = dedicated ? MemoryBlock::createDedicated(
                                  _physicalDevice,
                                  _device,
                                  vk::MemoryAllocateInfo {.allocationSize = size, .memoryTypeIndex = memoryType},
                                  request.resource)
                            : MemoryBlock::create(_physicalDevice, _device, size, memoryType);
    if (!memory)
    {
        return std::unexpected {std::move(memory.error())};
    }
    const auto range = (*memory)->allocate(request.requirements.size, request.requirements.alignment);
    if (!range)
    {
        return std::unexpected {range.error()};
    }
    _blocks.push_back(Block {.memory = std::move(*memory),
                             .memoryType = memoryType,
                             .image = static_cast<bool>(request.resource.image),
                             .dedicated = dedicated});
    ++_stats.allocationCount;
    _stats.committedBytes += size;
    _heapCommitted.at(_memoryProperties.memoryTypes.at(memoryType).heapIndex) += size;
    return publish(_blocks.back(), *range);
}

auto MemoryPool::publish(Block& block, MemoryRange range) -> MemoryAllocation
{
    const auto id = _nextId++;
    _allocations.emplace(id, Allocation {.block = &block, .range = range});
    _stats.liveBytes += range.size;
    return MemoryAllocation {*this, id};
}

auto MemoryPool::getAllocation(std::uint64_t id) const -> const Allocation&
{
    const auto position = _allocations.find(id);
    expect(position != _allocations.end(), "Memory allocation id is not live in its pool");
    return position->second;
}

void MemoryPool::release(std::uint64_t id) noexcept
{
    const auto allocation = getAllocation(id);
    const auto released = allocation.block->memory->release(allocation.range);
    expect(released.has_value(), "Memory pool lost its reserved allocation range");
    _stats.liveBytes -= allocation.range.size;
    _allocations.erase(id);
    releaseEmpty(*allocation.block);
}

void MemoryPool::releaseEmpty(Block& block)
{
    if (block.memory->getFreeSize() != block.memory->getCapacity())
    {
        return;
    }
    const auto anotherEmpty = std::ranges::any_of(_blocks, [&](const Block& candidate) {
        return &candidate != &block && !candidate.dedicated && candidate.memoryType == block.memoryType &&
               candidate.image == block.image && candidate.memory->getFreeSize() == candidate.memory->getCapacity();
    });
    if (block.dedicated || anotherEmpty)
    {
        eraseBlock(std::ranges::find_if(_blocks, [&](const Block& candidate) {
            return &candidate == &block;
        }));
    }
}

void MemoryPool::eraseBlock(const std::list<Block>::iterator& position)
{
    const auto size = position->memory->getCapacity();
    _stats.committedBytes -= size;
    --_stats.allocationCount;
    _heapCommitted.at(_memoryProperties.memoryTypes.at(position->memoryType).heapIndex) -= size;
    _blocks.erase(position);
}

void MemoryPool::reclaimEmptyBlocks()
{
    auto position = _blocks.begin();
    while (position != _blocks.end())
    {
        if (position->memory->getFreeSize() == position->memory->getCapacity())
        {
            const auto removed = position++;
            eraseBlock(removed);
        }
        else
        {
            ++position;
        }
    }
}

auto MemoryPool::getStats() const -> const MemoryPoolStats&
{
    return _stats;
}

auto MemoryPool::getPhysicalDevice() const noexcept -> const vk::raii::PhysicalDevice&
{
    return _physicalDevice;
}
}

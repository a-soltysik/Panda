// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "resources/memory/MemoryBlock.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "VulkanError.hpp"
#include "resources/memory/MemoryProperties.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{
namespace
{
struct BlockConfiguration
{
    bool hostVisible {false};
    std::uint64_t atomSize {1};
    bool needsCacheManagement {false};
};

auto configureMapping(vk::MemoryPropertyFlags flags, std::uint64_t size, const vk::PhysicalDeviceLimits& limits)
    -> Result<BlockConfiguration>
{
    if (flags & unsupportedMemoryProperties)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Memory property requires an unsupported allocation feature")};
    }
    const auto hostVisible = static_cast<bool>(flags & vk::MemoryPropertyFlagBits::eHostVisible);
    const auto needsCache = hostVisible && !(flags & vk::MemoryPropertyFlagBits::eHostCoherent);
    const auto atomSize = needsCache ? limits.nonCoherentAtomSize : 1;
    if ((hostVisible && size > std::numeric_limits<std::size_t>::max()) || !std::has_single_bit(atomSize))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Memory block cannot be mapped with the reported limits")};
    }
    return BlockConfiguration {.hostVisible = hostVisible, .atomSize = atomSize, .needsCacheManagement = needsCache};
}

auto configureBlock(const vk::raii::PhysicalDevice& physicalDevice, std::uint64_t size, std::uint32_t memoryTypeIndex)
    -> Result<BlockConfiguration>
{
    const auto memory = physicalDevice.getMemoryProperties();
    if (size == 0 || memoryTypeIndex >= memory.memoryTypeCount)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid memory block size or memory type")};
    }
    const auto type = memory.memoryTypes.at(memoryTypeIndex);
    const auto properties =
        physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceMaintenance3Properties>();
    const auto maximum = properties.get<vk::PhysicalDeviceMaintenance3Properties>().maxMemoryAllocationSize;
    if (type.heapIndex >= memory.memoryHeapCount || size > memory.memoryHeaps.at(type.heapIndex).size || size > maximum)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Memory block exceeds the heap or allocation limit")};
    }
    return configureMapping(type.propertyFlags,
                            size,
                            properties.get<vk::PhysicalDeviceProperties2>().properties.limits);
}

auto roundSize(std::uint64_t size, std::uint64_t alignment) -> Result<std::uint64_t>
{
    const auto remainder = size % alignment;
    const auto padding = remainder == 0 ? 0 : alignment - remainder;
    if (size > std::numeric_limits<std::uint64_t>::max() - padding)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Atom-rounded allocation size overflows")};
    }
    return size + padding;
}
}

auto MemoryBlock::create(const vk::raii::PhysicalDevice& physicalDevice,
                         const vk::raii::Device& device,
                         std::uint64_t size,
                         std::uint32_t memoryTypeIndex) -> Result<std::unique_ptr<MemoryBlock>>
{
    return createInternal(physicalDevice,
                          device,
                          vk::MemoryAllocateInfo {.allocationSize = size, .memoryTypeIndex = memoryTypeIndex});
}

// NOLINTNEXTLINE(readability-function-size): These inputs map directly to Vulkan's dedicated export allocation.
auto MemoryBlock::createDedicated(const vk::raii::PhysicalDevice& physicalDevice,
                                  const vk::raii::Device& device,
                                  vk::MemoryAllocateInfo allocation,
                                  const vk::MemoryDedicatedAllocateInfo& resource,
                                  vk::ExternalMemoryHandleTypeFlags exportHandleTypes)
    -> Result<std::unique_ptr<MemoryBlock>>
{
    if (static_cast<bool>(resource.buffer) == static_cast<bool>(resource.image) || resource.pNext != nullptr ||
        allocation.pNext != nullptr)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Dedicated memory requires exactly one resource")};
    }
    auto dedicatedInfo = resource;
    const auto exportInfo = vk::ExportMemoryAllocateInfo {.pNext = &dedicatedInfo, .handleTypes = exportHandleTypes};
    if (exportHandleTypes)
    {
        allocation.pNext = &exportInfo;
    }
    else
    {
        allocation.pNext = &dedicatedInfo;
    }
    return createInternal(physicalDevice, device, allocation);
}

auto MemoryBlock::createInternal(const vk::raii::PhysicalDevice& physicalDevice,
                                 const vk::raii::Device& device,
                                 const vk::MemoryAllocateInfo& info) -> Result<std::unique_ptr<MemoryBlock>>
{
    const auto configuration = configureBlock(physicalDevice, info.allocationSize, info.memoryTypeIndex);
    if (!configuration)
    {
        return std::unexpected {configuration.error()};
    }
    auto memory = core::vulkan::detail::checkedCreation(device.allocateMemory(info));
    if (!memory)
    {
        return std::unexpected {std::move(memory.error())};
    }
    auto block = std::unique_ptr<MemoryBlock> {
        new MemoryBlock {device,
                         std::move(*memory),
                         {.size = info.allocationSize,
                          .atomSize = configuration->atomSize,
                          .needsCacheManagement = configuration->needsCacheManagement,
                          .dedicated = info.pNext != nullptr}}
    };
    if (configuration->hostVisible)
    {
        const auto mapped = block->map();
        if (!mapped)
        {
            return std::unexpected {mapped.error()};
        }
    }
    return block;
}

MemoryBlock::MemoryBlock(const vk::raii::Device& device, vk::raii::DeviceMemory memory, Settings settings)
    : _device {device},
      _memory {std::move(memory)},
      _ranges {settings.size},
      _atomSize {settings.atomSize},
      _needsCacheManagement {settings.needsCacheManagement},
      _dedicated {settings.dedicated}
{
}

MemoryBlock::~MemoryBlock()
{
    expect(_ranges.getFreeSize() == _ranges.getCapacity(), "Memory block destroyed with live allocations");
    if (!_mapped.empty())
    {
        _memory.unmapMemory();
    }
}

auto MemoryBlock::map() -> Result<void>
{
    auto* mapped = static_cast<void*>(nullptr);
    // The enhanced Hpp map overload moves its undefined output on failure.
    // Inspect the status with the raw overload before reading the output.
    const auto result = (*_device).mapMemory(*_memory, 0, _ranges.getCapacity(), {}, &mapped, *_device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    expect(mapped != nullptr, "Vulkan returned a null successful memory mapping");
    _mapped = std::span {static_cast<std::byte*>(mapped), std::size_t {_ranges.getCapacity()}};
    return {};
}

auto MemoryBlock::allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>
{
    if (size == 0 || !std::has_single_bit(alignment))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid memory slice size or alignment")};
    }
    if (_dedicated)
    {
        if (size != _ranges.getCapacity())
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument, "Dedicated memory cannot be subdivided")};
        }
        return _ranges.allocate(size, alignment);
    }
    return roundSize(size, _atomSize).and_then([&](std::uint64_t rounded) {
        return _ranges.allocate(rounded, std::max(alignment, _atomSize));
    });
}

auto MemoryBlock::release(MemoryRange range) -> Result<void>
{
    return _ranges.release(range);
}

auto MemoryBlock::getMappedBytes(MemoryRange range) const -> Result<std::span<std::byte>>
{
    if (!_ranges.isLive(range))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mapped access requires an exact live slice")};
    }
    if (_mapped.empty())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Memory block is not host-visible")};
    }
    return _mapped.subspan(static_cast<std::size_t>(range.offset), static_cast<std::size_t>(range.size));
}

auto MemoryBlock::mappedRange(MemoryRange range, std::uint64_t offset, std::uint64_t size) const
    -> Result<vk::MappedMemoryRange>
{
    const auto bytes = getMappedBytes(range);
    if (!bytes)
    {
        return std::unexpected {bytes.error()};
    }
    if (size == 0 || offset > range.size || size > range.size - offset)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cache operation exceeds the live memory slice")};
    }
    const auto begin = offset - (offset % _atomSize);
    // A dedicated allocation may end within an atom: Vulkan permits the final
    // operation to end at the memory object's boundary instead of a full atom.
    const auto finish = offset + size;
    const auto padding = finish % _atomSize == 0 ? 0 : _atomSize - (finish % _atomSize);
    const auto end = finish + std::min(padding, range.size - finish);
    expect(_dedicated || end % _atomSize == 0, "Cache operation rounding exceeds its reserved memory slice");
    return vk::MappedMemoryRange {.memory = *_memory, .offset = range.offset + begin, .size = end - begin};
}

auto MemoryBlock::flush(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    const auto mapped = mappedRange(range, offset, size);
    if (!mapped)
    {
        return std::unexpected {mapped.error()};
    }
    const auto result = _needsCacheManagement ? _device.flushMappedMemoryRanges(*mapped) : vk::Result::eSuccess;
    return result == vk::Result::eSuccess ? Result<void> {} : std::unexpected {makeVulkanError(result)};
}

auto MemoryBlock::invalidate(MemoryRange range, std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    const auto mapped = mappedRange(range, offset, size);
    if (!mapped)
    {
        return std::unexpected {mapped.error()};
    }
    const auto result = _needsCacheManagement ? _device.invalidateMappedMemoryRanges(*mapped) : vk::Result::eSuccess;
    return result == vk::Result::eSuccess ? Result<void> {} : std::unexpected {makeVulkanError(result)};
}

auto MemoryBlock::getMemory() const -> vk::DeviceMemory
{
    return *_memory;
}

auto MemoryBlock::getFreeSize() const -> std::uint64_t
{
    return _ranges.getFreeSize();
}

auto MemoryBlock::getCapacity() const -> std::uint64_t
{
    return _ranges.getCapacity();
}

}

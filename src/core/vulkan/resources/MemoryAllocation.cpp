// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "MemoryAllocation.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "MemoryBlock.hpp"
#include "MemoryPool.hpp"
#include "RangeAllocator.hpp"

namespace panda::detail
{
MemoryAllocation::MemoryAllocation() = default;

MemoryAllocation::MemoryAllocation(MemoryPool& pool, std::uint64_t id)
    : _pool {&pool},
      _id {id}
{
}

MemoryAllocation::MemoryAllocation(MemoryAllocation&& other) noexcept
    : _pool {std::exchange(other._pool, nullptr)},
      _id {std::exchange(other._id, 0)}
{
}

auto MemoryAllocation::operator=(MemoryAllocation&& other) noexcept -> MemoryAllocation&
{
    if (this != &other)
    {
        reset();
        _pool = std::exchange(other._pool, nullptr);
        _id = std::exchange(other._id, 0);
    }
    return *this;
}

MemoryAllocation::~MemoryAllocation()
{
    reset();
}

auto MemoryAllocation::isValid() const -> bool
{
    return _pool != nullptr;
}

auto MemoryAllocation::getMemory() const -> vk::DeviceMemory
{
    expect(isValid(), "Native access to an empty memory allocation");
    return _pool->getAllocation(_id).block->memory->getMemory();
}

auto MemoryAllocation::getRange() const -> MemoryRange
{
    expect(isValid(), "Range access to an empty memory allocation");
    return _pool->getAllocation(_id).range;
}

auto MemoryAllocation::getMappedBytes() const -> Result<std::span<std::byte>>
{
    if (!isValid())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mapped access to an empty memory allocation")};
    }
    const auto& allocation = _pool->getAllocation(_id);
    return allocation.block->memory->getMappedBytes(allocation.range);
}

auto MemoryAllocation::flush(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    const auto bytes = getMappedBytes();
    if (!bytes)
    {
        return std::unexpected {bytes.error()};
    }
    const auto& allocation = _pool->getAllocation(_id);
    return allocation.block->memory->flush(allocation.range, offset, size);
}

auto MemoryAllocation::invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    const auto bytes = getMappedBytes();
    if (!bytes)
    {
        return std::unexpected {bytes.error()};
    }
    const auto& allocation = _pool->getAllocation(_id);
    return allocation.block->memory->invalidate(allocation.range, offset, size);
}

void MemoryAllocation::reset() noexcept
{
    auto* pool = std::exchange(_pool, nullptr);
    const auto id = std::exchange(_id, 0);
    if (pool != nullptr)
    {
        pool->release(id);
    }
}
}

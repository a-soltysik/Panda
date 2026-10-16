// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
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
    return test::ScopedMock<test::MemoryAllocationMock>::getActiveMock().getMemory(_id);
}

auto MemoryAllocation::getRange() const -> MemoryRange
{
    return test::ScopedMock<test::MemoryAllocationMock>::getActiveMock().getRange(_id);
}

auto MemoryAllocation::getMappedBytes() const -> Result<std::span<std::byte>>
{
    return test::ScopedMock<test::MemoryAllocationMock>::getActiveMock().getMappedBytes(_id);
}

auto MemoryAllocation::flush(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return test::ScopedMock<test::MemoryAllocationMock>::getActiveMock().flush(_id, offset, size);
}

auto MemoryAllocation::invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return test::ScopedMock<test::MemoryAllocationMock>::getActiveMock().invalidate(_id, offset, size);
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

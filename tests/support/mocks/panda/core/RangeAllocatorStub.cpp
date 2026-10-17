#include <cstdint>
#include <panda/Error.hpp>

#include "ScopedMock.hpp"
#include "panda/core/RangeAllocatorMock.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace panda::core::vulkan::resources
{
RangeAllocator::RangeAllocator(std::uint64_t capacity)
    : _capacity {capacity},
      _freeSize {capacity}
{
    test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().construct(capacity);
}

auto operator==(MemoryRange left, MemoryRange right) -> bool
{
    return left.offset == right.offset && left.size == right.size;
}

// These definitions replace non-static production members and must preserve
// their signatures even though the gMock boundary itself is process-scoped.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
// cppcheck-suppress functionStatic
auto RangeAllocator::allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>
{
    return test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().allocate(size, alignment);
}

// cppcheck-suppress functionStatic
auto RangeAllocator::release(MemoryRange range) -> Result<void>
{
    return test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().release(range);
}

// cppcheck-suppress functionStatic
auto RangeAllocator::isLive(MemoryRange range) const -> bool
{
    return test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().isLive(range.offset, range.size);
}

// cppcheck-suppress functionStatic
auto RangeAllocator::getCapacity() const -> std::uint64_t
{
    return test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().getCapacity();
}

// cppcheck-suppress functionStatic
auto RangeAllocator::getFreeSize() const -> std::uint64_t
{
    return test::ScopedMock<test::RangeAllocatorMock>::getActiveMock().getFreeSize();
}

// NOLINTEND(readability-convert-member-functions-to-static)
}

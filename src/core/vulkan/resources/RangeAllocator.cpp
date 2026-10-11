#include "RangeAllocator.hpp"

#include <bit>
#include <cstdint>
#include <expected>
#include <iterator>
#include <panda/Error.hpp>

namespace panda::detail
{

auto operator==(MemoryRange left, MemoryRange right) -> bool
{
    return left.offset == right.offset && left.size == right.size;
}

RangeAllocator::RangeAllocator(std::uint64_t capacity)
    : _capacity {capacity},
      _freeSize {capacity}
{
    if (capacity != 0)
    {
        _freeRanges.emplace(0, capacity);
    }
}

auto RangeAllocator::allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>
{
    if (size == 0 || !std::has_single_bit(alignment))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Allocation needs a nonzero size and power-of-two alignment")};
    }
    for (auto position = _freeRanges.begin(); position != _freeRanges.end(); ++position)
    {
        const auto [offset, available] = *position;
        const auto remainder = offset % alignment;
        const auto padding = remainder == 0 ? 0 : alignment - remainder;
        // Subtraction proves both the aligned offset and allocation end fit.
        if (padding <= available && size <= available - padding)
        {
            return carve(position, MemoryRange {.offset = offset + padding, .size = size});
        }
    }
    return std::unexpected {makeError(ErrorCode::ResourceExhausted, "No aligned range fits in the memory block")};
}

auto RangeAllocator::carve(const Ranges::iterator& position, MemoryRange allocation) -> MemoryRange
{
    const auto [offset, size] = *position;
    const auto prefix = allocation.offset - offset;
    const auto suffix = size - prefix - allocation.size;
    _liveRanges.emplace(allocation.offset, allocation.size);
    _freeRanges.erase(position);
    if (prefix != 0)
    {
        _freeRanges.emplace(offset, prefix);
    }
    if (suffix != 0)
    {
        _freeRanges.emplace(allocation.offset + allocation.size, suffix);
    }
    _freeSize -= allocation.size;
    return allocation;
}

auto RangeAllocator::release(MemoryRange range) -> Result<void>
{
    const auto position = _liveRanges.find(range.offset);
    if (position == _liveRanges.end() || position->second != range.size)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Release requires an exact live memory range")};
    }
    coalesce(range);
    _liveRanges.erase(position);
    _freeSize += range.size;
    return {};
}

void RangeAllocator::coalesce(MemoryRange range)
{
    auto next = _freeRanges.lower_bound(range.offset);
    if (next != _freeRanges.begin())
    {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == range.offset)
        {
            range.offset = previous->first;
            range.size += previous->second;
            _freeRanges.erase(previous);
        }
    }
    if (next != _freeRanges.end() && range.offset + range.size == next->first)
    {
        range.size += next->second;
        next = _freeRanges.erase(next);
    }
    _freeRanges.emplace_hint(next, range.offset, range.size);
}

auto RangeAllocator::getCapacity() const -> std::uint64_t
{
    return _capacity;
}

auto RangeAllocator::getFreeSize() const -> std::uint64_t
{
    return _freeSize;
}

}

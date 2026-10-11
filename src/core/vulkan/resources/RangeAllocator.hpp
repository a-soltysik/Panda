#pragma once

#include <cstdint>
#include <map>
#include <panda/Error.hpp>

namespace panda::detail
{

struct MemoryRange
{
    std::uint64_t offset {0};
    std::uint64_t size {0};
};

auto operator==(MemoryRange left, MemoryRange right) -> bool;

// CPU bookkeeping for one memory block. No Vulkan objects or synchronization.
// The owner releases ranges only after their GPU uses complete. Serialized use.
class RangeAllocator final
{
public:
    explicit RangeAllocator(std::uint64_t capacity);
    RangeAllocator(const RangeAllocator&) = delete;
    auto operator=(const RangeAllocator&) -> RangeAllocator& = delete;
    RangeAllocator(RangeAllocator&&) = delete;
    auto operator=(RangeAllocator&&) -> RangeAllocator& = delete;
    ~RangeAllocator() = default;

    // Alignment must be a nonzero power of two; failures leave the block intact.
    [[nodiscard]] auto allocate(std::uint64_t size, std::uint64_t alignment) -> Result<MemoryRange>;
    // Requires an exact live range from this block, once only. No device wait.
    [[nodiscard]] auto release(MemoryRange range) -> Result<void>;
    [[nodiscard]] auto isLive(MemoryRange range) const -> bool;
    [[nodiscard]] auto getCapacity() const -> std::uint64_t;
    [[nodiscard]] auto getFreeSize() const -> std::uint64_t;

private:
    using Ranges = std::map<std::uint64_t, std::uint64_t>;

    auto carve(const Ranges::iterator& position, MemoryRange allocation) -> MemoryRange;
    void coalesce(MemoryRange range);

    std::uint64_t _capacity;
    std::uint64_t _freeSize;
    Ranges _freeRanges;
    Ranges _liveRanges;
};

}

#include "RangeAllocator.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <panda/Error.hpp>

namespace
{
using panda::detail::MemoryRange;
using panda::detail::RangeAllocator;
}

TEST(RangeAllocator, ExactFitExhaustsAndReleaseRestoresBlock)
{
    auto allocator = RangeAllocator {64};
    const auto allocation = allocator.allocate(64, 1);
    ASSERT_TRUE(allocation);
    EXPECT_TRUE(allocator.isLive(*allocation));
    EXPECT_FALSE(allocator.isLive(MemoryRange {.offset = 0, .size = 63}));
    EXPECT_EQ(*allocation, (MemoryRange {.offset = 0, .size = 64}));
    EXPECT_EQ(allocator.getCapacity(), 64);
    EXPECT_EQ(allocator.getFreeSize(), 0);
    const auto exhausted = allocator.allocate(1, 1);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(allocator.release(*allocation));
    EXPECT_FALSE(allocator.isLive(*allocation));
    EXPECT_EQ(allocator.getFreeSize(), 64);
    const auto reused = allocator.allocate(64, 1);
    ASSERT_TRUE(reused);
    EXPECT_EQ(*reused, *allocation);
}

TEST(RangeAllocator, AlignmentPreservesPrefixAndSuffix)
{
    auto allocator = RangeAllocator {64};
    ASSERT_TRUE(allocator.allocate(3, 1));
    const auto aligned = allocator.allocate(8, 16);
    ASSERT_TRUE(aligned);
    EXPECT_EQ(aligned->offset, 16);
    const auto prefix = allocator.allocate(13, 1);
    ASSERT_TRUE(prefix);
    EXPECT_EQ(prefix->offset, 3);
    const auto suffix = allocator.allocate(40, 1);
    ASSERT_TRUE(suffix);
    EXPECT_EQ(suffix->offset, 24);
    EXPECT_EQ(allocator.getFreeSize(), 0);
}

TEST(RangeAllocator, FirstFitUsesLowestAddressThatFits)
{
    auto allocator = RangeAllocator {64};
    const auto first = allocator.allocate(8, 1);
    ASSERT_TRUE(first);
    ASSERT_TRUE(allocator.allocate(8, 1));
    const auto third = allocator.allocate(8, 1);
    ASSERT_TRUE(third);
    ASSERT_TRUE(allocator.release(*first));
    ASSERT_TRUE(allocator.release(*third));
    const auto large = allocator.allocate(16, 16);
    ASSERT_TRUE(large);
    EXPECT_EQ(large->offset, 16);
    const auto small = allocator.allocate(8, 8);
    ASSERT_TRUE(small);
    EXPECT_EQ(small->offset, 0);
}

TEST(RangeAllocator, SkipsHoleConsumedByAlignmentPadding)
{
    auto allocator = RangeAllocator {32};
    ASSERT_TRUE(allocator.allocate(1, 1));
    const auto hole = allocator.allocate(7, 1);
    ASSERT_TRUE(hole);
    ASSERT_TRUE(allocator.allocate(8, 1));
    ASSERT_TRUE(allocator.release(*hole));
    const auto aligned = allocator.allocate(8, 8);
    ASSERT_TRUE(aligned);
    EXPECT_EQ(aligned->offset, 16);
    EXPECT_EQ(allocator.getFreeSize(), 15);
}

TEST(RangeAllocator, CoalescesInEveryReleaseOrder)
{
    static constexpr auto orders = std::array {
        std::array {0, 1, 2},
        std::array {0, 2, 1},
        std::array {1, 0, 2},
        std::array {1, 2, 0},
        std::array {2, 0, 1},
        std::array {2, 1, 0}
    };
    for (const auto& order : orders)
    {
        auto allocator = RangeAllocator {24};
        const auto ranges = std::array {allocator.allocate(8, 1), allocator.allocate(8, 1), allocator.allocate(8, 1)};
        for (const auto index : order)
        {
            const auto& range = ranges.at(static_cast<std::size_t>(index));
            ASSERT_TRUE(range);
            ASSERT_TRUE(allocator.release(*range));
        }
        const auto merged = allocator.allocate(24, 1);
        ASSERT_TRUE(merged);
        EXPECT_EQ(*merged, (MemoryRange {.offset = 0, .size = 24}));
    }
}

TEST(RangeAllocator, FragmentationFailureLeavesStateIntact)
{
    auto allocator = RangeAllocator {24};
    const auto first = allocator.allocate(8, 1);
    ASSERT_TRUE(first);
    ASSERT_TRUE(allocator.allocate(8, 1));
    ASSERT_TRUE(allocator.release(*first));
    const auto fragmented = allocator.allocate(9, 1);
    ASSERT_FALSE(fragmented);
    EXPECT_EQ(fragmented.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(allocator.getFreeSize(), 16);
    const auto reused = allocator.allocate(8, 1);
    ASSERT_TRUE(reused);
    EXPECT_EQ(*reused, *first);
    const auto last = allocator.allocate(8, 1);
    ASSERT_TRUE(last);
    EXPECT_EQ(last->offset, 16);
}

TEST(RangeAllocator, InvalidRequestsDoNotConsumeSpace)
{
    auto allocator = RangeAllocator {16};
    const auto requests = std::array {
        std::array {0, 1},
        std::array {1, 0},
        std::array {1, 3}
    };
    for (const auto& request : requests)
    {
        const auto result =
            allocator.allocate(static_cast<std::uint64_t>(request.at(0)), static_cast<std::uint64_t>(request.at(1)));
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
        EXPECT_EQ(allocator.getFreeSize(), 16);
    }
    ASSERT_TRUE(allocator.allocate(16, 1));
}

TEST(RangeAllocator, RejectsPartialUnknownAndDuplicateReleases)
{
    auto allocator = RangeAllocator {16};
    const auto allocation = allocator.allocate(8, 1);
    ASSERT_TRUE(allocation);
    const auto invalid = std::array {
        MemoryRange {.offset = 0, .size = 7},
        MemoryRange {.offset = 1, .size = 7},
        MemoryRange {.offset = 8, .size = 8}
    };
    for (const auto range : invalid)
    {
        const auto result = allocator.release(range);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
        EXPECT_EQ(allocator.getFreeSize(), 8);
    }
    ASSERT_TRUE(allocator.release(*allocation));
    const auto duplicate = allocator.release(*allocation);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(allocator.getFreeSize(), 16);
    ASSERT_TRUE(allocator.allocate(16, 1));
}

TEST(RangeAllocator, EmptyBlockReportsExhaustion)
{
    auto allocator = RangeAllocator {0};
    const auto result = allocator.allocate(1, 1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(allocator.getCapacity(), 0);
    EXPECT_EQ(allocator.getFreeSize(), 0);
}

TEST(RangeAllocator, AlignmentOverflowCannotWrapToLowAddress)
{
    static constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto allocator = RangeAllocator {maximum};
    const auto almostFull = allocator.allocate(maximum - 3, 1);
    ASSERT_TRUE(almostFull);
    const auto overflow = allocator.allocate(1, 8);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(allocator.getFreeSize(), 3);
    const auto tail = allocator.allocate(3, 1);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail->offset, maximum - 3);
    ASSERT_TRUE(allocator.release(*almostFull));
    ASSERT_TRUE(allocator.release(*tail));
    ASSERT_TRUE(allocator.allocate(maximum, 1));
}

TEST(RangeAllocator, SizeOverflowAndLargestAlignmentLeaveStateIntact)
{
    static constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    static constexpr auto largestAlignment = std::uint64_t {1} << 63U;
    auto allocator = RangeAllocator {maximum};
    ASSERT_TRUE(allocator.allocate(1, 1));
    EXPECT_FALSE(allocator.allocate(maximum, 1));
    EXPECT_FALSE(allocator.allocate(largestAlignment, largestAlignment));
    EXPECT_EQ(allocator.getFreeSize(), maximum - 1);
    const auto high = allocator.allocate(1, largestAlignment);
    ASSERT_TRUE(high);
    EXPECT_EQ(high->offset, largestAlignment);
    EXPECT_EQ(allocator.getFreeSize(), maximum - 2);
}

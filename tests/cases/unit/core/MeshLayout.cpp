#include "MeshLayout.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

TEST(MeshLayout, ComputesVertexAndSixteenBitIndexStorage)
{
    const auto layout = panda::detail::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 20,
        .indices = panda::MeshIndexDesc {.count = 6, .type = panda::MeshIndexType::UInt16}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexSize(), 60);
    EXPECT_EQ(layout->getIndexSize(), 12);
}

TEST(MeshLayout, ComputesThirtyTwoBitIndexStorage)
{
    const auto layout = panda::detail::MeshLayout::create({
        .vertexCount = 4,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 9, .type = panda::MeshIndexType::UInt32}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexSize(), 48);
    EXPECT_EQ(layout->getIndexSize(), 36);
}

TEST(MeshLayout, NonIndexedMeshHasNoIndexStorage)
{
    const auto layout =
        panda::detail::MeshLayout::create({.vertexCount = 3, .vertexStride = 16, .indices = std::nullopt});

    ASSERT_TRUE(layout);
    EXPECT_FALSE(layout->getIndices().has_value());
    EXPECT_EQ(layout->getIndexSize(), 0);
}

TEST(MeshLayout, MaximumPublicCountsProduceExact64BitStorageSizes)
{
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    const auto layout = panda::detail::MeshLayout::create({
        .vertexCount = maximum,
        .vertexStride = maximum,
        .indices = panda::MeshIndexDesc {.count = maximum, .type = panda::MeshIndexType::UInt32}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexSize(), static_cast<std::uint64_t>(maximum) * maximum);
    EXPECT_EQ(layout->getIndexSize(), static_cast<std::uint64_t>(maximum) * 4);
}

TEST(MeshLayout, RejectsMissingVertexCountOrStride)
{
    const auto noVertices =
        panda::detail::MeshLayout::create({.vertexCount = 0, .vertexStride = 12, .indices = std::nullopt});
    const auto noStride =
        panda::detail::MeshLayout::create({.vertexCount = 3, .vertexStride = 0, .indices = std::nullopt});

    ASSERT_FALSE(noVertices);
    ASSERT_FALSE(noStride);
    EXPECT_EQ(noVertices.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(noStride.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(MeshLayout, RejectsEmptyIndexDescription)
{
    const auto layout = panda::detail::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 0, .type = panda::MeshIndexType::UInt16}
    });

    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(MeshLayout, RejectsUnknownIndexType)
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalidType = static_cast<panda::MeshIndexType>(255);
    const auto layout = panda::detail::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 3, .type = invalidType}
    });

    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, panda::ErrorCode::InvalidArgument);
}

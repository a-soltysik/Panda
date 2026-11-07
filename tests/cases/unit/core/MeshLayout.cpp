#include "MeshLayout.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <utility>
#include <vector>

TEST(MeshLayout, ComputesVertexAndSixteenBitIndexStorage)
{
    const auto layout = panda::core::vulkan::resources::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 20,
        .indices = panda::MeshIndexDesc {.count = 6, .type = panda::MeshIndexType::UInt16},
        .attributes = {}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexCount(), 3);
    EXPECT_EQ(layout->getVertexStride(), 20);
    EXPECT_EQ(layout->getVertexSize(), 60);
    ASSERT_TRUE(layout->getIndices().has_value());
    const auto indices = layout->getIndices().value_or(panda::MeshIndexDesc {});
    EXPECT_EQ(indices.count, 6);
    EXPECT_EQ(indices.type, panda::MeshIndexType::UInt16);
    EXPECT_EQ(layout->getIndexSize(), 12);
}

TEST(MeshLayout, ComputesThirtyTwoBitIndexStorage)
{
    const auto layout = panda::core::vulkan::resources::MeshLayout::create({
        .vertexCount = 4,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 9, .type = panda::MeshIndexType::UInt32},
        .attributes = {}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexCount(), 4);
    EXPECT_EQ(layout->getVertexStride(), 12);
    EXPECT_EQ(layout->getVertexSize(), 48);
    ASSERT_TRUE(layout->getIndices().has_value());
    const auto indices = layout->getIndices().value_or(panda::MeshIndexDesc {});
    EXPECT_EQ(indices.count, 9);
    EXPECT_EQ(indices.type, panda::MeshIndexType::UInt32);
    EXPECT_EQ(layout->getIndexSize(), 36);
}

TEST(MeshLayout, NonIndexedMeshHasNoIndexStorage)
{
    const auto layout = panda::core::vulkan::resources::MeshLayout::create(
        {.vertexCount = 3, .vertexStride = 16, .indices = std::nullopt, .attributes = {}});

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexCount(), 3);
    EXPECT_EQ(layout->getVertexStride(), 16);
    EXPECT_EQ(layout->getVertexSize(), 48);
    EXPECT_FALSE(layout->getIndices().has_value());
    EXPECT_EQ(layout->getIndexSize(), 0);
}

TEST(MeshLayout, MaximumPublicCountsProduceExact64BitStorageSizes)
{
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    const auto layout = panda::core::vulkan::resources::MeshLayout::create({
        .vertexCount = maximum,
        .vertexStride = maximum,
        .indices = panda::MeshIndexDesc {.count = maximum, .type = panda::MeshIndexType::UInt32},
        .attributes = {}
    });

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexSize(), static_cast<std::uint64_t>(maximum) * maximum);
    EXPECT_EQ(layout->getIndexSize(), static_cast<std::uint64_t>(maximum) * 4);
}

TEST(MeshLayout, RejectsMissingVertexCountOrStride)
{
    const auto noVertices = panda::core::vulkan::resources::MeshLayout::create(
        {.vertexCount = 0, .vertexStride = 12, .indices = std::nullopt, .attributes = {}});
    const auto noStride = panda::core::vulkan::resources::MeshLayout::create(
        {.vertexCount = 3, .vertexStride = 0, .indices = std::nullopt, .attributes = {}});

    ASSERT_FALSE(noVertices);
    ASSERT_FALSE(noStride);
    EXPECT_EQ(noVertices.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(noStride.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(MeshLayout, RejectsEmptyIndexDescription)
{
    const auto layout = panda::core::vulkan::resources::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 0, .type = panda::MeshIndexType::UInt16},
        .attributes = {}
    });

    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(MeshLayout, RejectsUnknownIndexType)
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalidType = static_cast<panda::MeshIndexType>(255);
    const auto layout = panda::core::vulkan::resources::MeshLayout::create({
        .vertexCount = 3,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 3, .type = invalidType},
        .attributes = {}
    });

    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(MeshLayout, RetainsPositionColorLayoutForCompactUnlitMesh)
{
    const auto attributes = std::vector {
        panda::VertexAttribute {.semantic = panda::VertexAttributeSemantic::Position,
                                .format = panda::VertexAttributeFormat::Float3,
                                .offset = 0 },
        panda::VertexAttribute {.semantic = panda::VertexAttributeSemantic::Color,
                                .format = panda::VertexAttributeFormat::Float4,
                                .offset = 12}
    };
    const auto layout = panda::core::vulkan::resources::MeshLayout::create(
        {.vertexCount = 3, .vertexStride = 28, .indices = std::nullopt, .attributes = attributes});

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getVertexAttributes(), attributes);
}

TEST(MeshLayout, RejectsInvalidAttributeSemanticsFormatsBoundsAndOverlap)
{
    using panda::VertexAttribute;
    using panda::VertexAttributeFormat;
    using panda::VertexAttributeSemantic;
    const auto position = VertexAttribute {.semantic = VertexAttributeSemantic::Position,
                                           .format = VertexAttributeFormat::Float3,
                                           .offset = 0};
    const auto color = VertexAttribute {.semantic = VertexAttributeSemantic::Color,
                                        .format = VertexAttributeFormat::Float4,
                                        .offset = 12};
    const auto expectInvalid = [](std::vector<VertexAttribute> attributes, std::uint32_t stride) {
        const auto layout = panda::core::vulkan::resources::MeshLayout::create(
            {.vertexCount = 3, .vertexStride = stride, .indices = std::nullopt, .attributes = std::move(attributes)});
        ASSERT_FALSE(layout);
        EXPECT_EQ(layout.error().code, panda::ErrorCode::InvalidArgument);
    };

    expectInvalid({color}, 28);
    expectInvalid({position, color}, 27);
    expectInvalid(
        {
            position,
            {.semantic = VertexAttributeSemantic::Normal, .format = VertexAttributeFormat::Float4, .offset = 12}
    },
        28);
    expectInvalid({position, position}, 28);
    expectInvalid(
        {
            position,
            {.semantic = VertexAttributeSemantic::Color, .format = VertexAttributeFormat::Float4, .offset = 8}
    },
        24);
}

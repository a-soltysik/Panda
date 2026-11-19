#include "TextureMipGeneration.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <panda/base/Error.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <vector>

namespace
{
auto byte(std::uint8_t value) -> std::byte
{
    return static_cast<std::byte>(value);
}

auto value(std::byte byteValue) -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(byteValue);
}
}

TEST(TextureMipGeneration, AveragesSrgbColorInLinearLightAndAlphaLinearly)
{
    const auto source = std::array {byte(0),
                                    byte(0),
                                    byte(0),
                                    byte(0),
                                    byte(255),
                                    byte(255),
                                    byte(255),
                                    byte(64),
                                    byte(255),
                                    byte(255),
                                    byte(255),
                                    byte(128),
                                    byte(0),
                                    byte(0),
                                    byte(0),
                                    byte(255)};

    const auto chain = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Srgb, .width = 2, .height = 2, .baseLevel = source, .rowPitchBytes = 0});

    ASSERT_TRUE(chain);
    ASSERT_EQ(chain->size(), 2U);
    ASSERT_EQ(chain->at(1).size(), 4U);
    EXPECT_EQ(value(chain->at(1).at(0)), 188U);
    EXPECT_EQ(value(chain->at(1).at(1)), 188U);
    EXPECT_EQ(value(chain->at(1).at(2)), 188U);
    EXPECT_EQ(value(chain->at(1).at(3)), 112U);
}

TEST(TextureMipGeneration, TreatsUnormChannelsAsLinearAndKeepsPaddedRowsOutOfTheChain)
{
    const auto source = std::array {byte(0),
                                    byte(0),
                                    byte(0),
                                    byte(0),
                                    byte(200),
                                    byte(200),
                                    byte(200),
                                    byte(200),
                                    byte(255),
                                    byte(255),
                                    byte(255),
                                    byte(255),
                                    byte(90),
                                    byte(90),
                                    byte(90),
                                    byte(90)};

    const auto chain = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm, .width = 1, .height = 2, .baseLevel = source, .rowPitchBytes = 8});

    ASSERT_TRUE(chain);
    ASSERT_EQ(chain->size(), 2U);
    EXPECT_EQ(
        chain->front(),
        (std::vector<std::byte> {byte(0), byte(0), byte(0), byte(0), byte(255), byte(255), byte(255), byte(255)}));
    EXPECT_EQ(value(chain->at(1).at(0)), 128U);
    EXPECT_EQ(value(chain->at(1).at(3)), 128U);
}

TEST(TextureMipGeneration, RejectsShortOrUnsupportedSources)
{
    const auto shortSource = std::array {byte(0), byte(0), byte(0)};
    const auto shortResult =
        panda::core::vulkan::upload::generateTextureMipChain({.format = panda::PixelFormat::Rgba8Unorm,
                                                              .width = 1,
                                                              .height = 1,
                                                              .baseLevel = shortSource,
                                                              .rowPitchBytes = 0});
    EXPECT_FALSE(shortResult);

    const auto unsupported =
        panda::core::vulkan::upload::generateTextureMipChain({.format = panda::PixelFormat::Undefined,
                                                              .width = 1,
                                                              .height = 1,
                                                              .baseLevel = shortSource,
                                                              .rowPitchBytes = 0});
    EXPECT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST(TextureMipGeneration, RejectsInvalidExtentsAndRowPitches)
{
    const auto pixel = std::array {byte(0), byte(0), byte(0), byte(255)};
    const auto zeroWidth = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm, .width = 0, .height = 1, .baseLevel = pixel});
    const auto zeroHeight = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm, .width = 1, .height = 0, .baseLevel = pixel});
    const auto oversized =
        panda::core::vulkan::upload::generateTextureMipChain({.format = panda::PixelFormat::Rgba8Unorm,
                                                              .width = std::numeric_limits<std::uint32_t>::max(),
                                                              .height = std::numeric_limits<std::uint32_t>::max(),
                                                              .baseLevel = {}});
    const auto narrowPitch = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm, .width = 1, .height = 1, .baseLevel = pixel, .rowPitchBytes = 2});
    const auto unalignedPitch = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm, .width = 1, .height = 2, .baseLevel = pixel, .rowPitchBytes = 6});
    const auto overflowingPitch = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Rgba8Unorm,
         .width = 1,
         .height = 2,
         .baseLevel = {},
         .rowPitchBytes = std::numeric_limits<std::uint64_t>::max() - 3U});

    ASSERT_FALSE(zeroWidth);
    ASSERT_FALSE(zeroHeight);
    ASSERT_FALSE(oversized);
    ASSERT_FALSE(narrowPitch);
    ASSERT_FALSE(unalignedPitch);
    ASSERT_FALSE(overflowingPitch);
    EXPECT_EQ(zeroWidth.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(zeroHeight.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(oversized.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(narrowPitch.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(unalignedPitch.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(overflowingPitch.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(TextureMipGeneration, PreservesBlackSrgbAndAcceptsSingleLevelBgra)
{
    const auto black = std::array {byte(0), byte(0), byte(0), byte(0), byte(0), byte(0), byte(0), byte(0)};
    const auto generated = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Bgra8Srgb, .width = 2, .height = 1, .baseLevel = black});
    ASSERT_TRUE(generated);
    ASSERT_EQ(generated->size(), 2U);
    EXPECT_EQ(generated->at(1), (std::vector<std::byte> {byte(0), byte(0), byte(0), byte(0)}));

    const auto singlePixel = std::array {byte(10), byte(20), byte(30), byte(40)};
    const auto singleLevel = panda::core::vulkan::upload::generateTextureMipChain(
        {.format = panda::PixelFormat::Bgra8Unorm, .width = 1, .height = 1, .baseLevel = singlePixel});
    ASSERT_TRUE(singleLevel);
    EXPECT_EQ(singleLevel->size(), 1U);
}

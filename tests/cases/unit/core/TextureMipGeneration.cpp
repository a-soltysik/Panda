#include "TextureMipGeneration.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "SwapchainGeneration.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <panda/Context.hpp>

namespace
{
auto variableExtentCapabilities() -> vk::SurfaceCapabilitiesKHR
{
    return {
        .minImageCount = 1,
        .maxImageCount = 3,
        .currentExtent = {.width = std::numeric_limits<std::uint32_t>::max(),
                          .height = std::numeric_limits<std::uint32_t>::max()               },
        .minImageExtent = {.width = 64,                                        .height = 48  },
        .maxImageExtent = {.width = 1920,                                      .height = 1080},
        .supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment
    };
}

constexpr auto formats = std::array {
    vk::SurfaceFormatKHR {.format = vk::Format::eR8G8B8A8Unorm, .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear},
    vk::SurfaceFormatKHR {.format = vk::Format::eB8G8R8A8Srgb,  .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear}
};
}

TEST(SwapchainGeneration, SelectsPreferredSrgbFormatAndClampsVariableExtent)
{
    const auto selected =
        panda::detail::chooseSurfaceConfig(variableExtentCapabilities(), formats, {.width = 4000, .height = 12});
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->format.format, vk::Format::eB8G8R8A8Srgb);
    EXPECT_EQ(selected->extent.width, 1920U);
    EXPECT_EQ(selected->extent.height, 48U);
    EXPECT_EQ(selected->imageCount, 2U);
}

TEST(SwapchainGeneration, HonorsFixedExtentAndSupportedImageCount)
{
    auto capabilities = variableExtentCapabilities();
    capabilities.currentExtent = vk::Extent2D {.width = 800, .height = 600};
    capabilities.maxImageCount = 1;
    const auto supported = std::array {
        vk::SurfaceFormatKHR {.format = vk::Format::eR8G8B8A8Unorm, .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear}
    };
    const auto selected = panda::detail::chooseSurfaceConfig(capabilities, supported, {.width = 1280, .height = 720});
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->format.format, vk::Format::eR8G8B8A8Unorm);
    EXPECT_EQ(selected->extent.width, 800U);
    EXPECT_EQ(selected->extent.height, 600U);
    EXPECT_EQ(selected->imageCount, 1U);
}

TEST(SwapchainGeneration, RejectsUnsupportedColorAttachmentAndFormat)
{
    auto capabilities = variableExtentCapabilities();
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eTransferDst;
    auto selected = panda::detail::chooseSurfaceConfig(capabilities, formats, {.width = 800, .height = 600});
    ASSERT_FALSE(selected.has_value());
    EXPECT_EQ(selected.error().code, panda::ErrorCode::Unsupported);
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;
    const auto hdrOnly = std::array {
        vk::SurfaceFormatKHR {.format = vk::Format::eR16G16B16A16Sfloat,
                              .colorSpace = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT}
    };
    selected = panda::detail::chooseSurfaceConfig(capabilities, hdrOnly, {.width = 800, .height = 600});
    ASSERT_FALSE(selected.has_value());
    EXPECT_EQ(selected.error().code, panda::ErrorCode::Unsupported);
}

TEST(SwapchainGeneration, RejectsZeroFixedFramebufferExtent)
{
    auto capabilities = variableExtentCapabilities();
    capabilities.currentExtent = vk::Extent2D {.width = 0, .height = 0};
    const auto selected = panda::detail::chooseSurfaceConfig(capabilities, formats, {.width = 800, .height = 600});
    ASSERT_FALSE(selected.has_value());
    EXPECT_EQ(selected.error().code, panda::ErrorCode::Unsupported);
}

TEST(SwapchainGeneration, EncodesLinearClearColorExactlyOnce)
{
    const auto srgb = panda::detail::encodedClear(vk::Format::eB8G8R8A8Srgb);
    const auto unorm = panda::detail::encodedClear(vk::Format::eB8G8R8A8Unorm);
    EXPECT_NEAR(srgb.float32.at(0), 0.025F, 0.0001F);
    EXPECT_NEAR(srgb.float32.at(1), 0.035F, 0.0001F);
    EXPECT_NEAR(srgb.float32.at(2), 0.05F, 0.0001F);
    EXPECT_GT(unorm.float32.at(0), srgb.float32.at(0));
    EXPECT_LT(unorm.float32.at(0), 0.2F);
    EXPECT_EQ(srgb.float32.at(3), 1.0F);
    EXPECT_EQ(unorm.float32.at(3), 1.0F);
}

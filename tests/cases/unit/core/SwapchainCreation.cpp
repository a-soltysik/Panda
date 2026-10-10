// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <panda/Error.hpp>
#include <span>
#include <string>
#include <tuple>
#include <utility>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

enum class Failure : std::uint8_t
{
    None,
    Capabilities,
    Formats,
    Configuration,
    Swapchain,
    Images,
    View,
    Semaphore,
    Fence
};

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto failureName(const testing::TestParamInfo<Failure>& info) -> std::string
{
    switch (info.param)
    {
    case Failure::None:
        return "None";
    case Failure::Capabilities:
        return "Capabilities";
    case Failure::Formats:
        return "Formats";
    case Failure::Configuration:
        return "Configuration";
    case Failure::Swapchain:
        return "Swapchain";
    case Failure::Images:
        return "Images";
    case Failure::View:
        return "View";
    case Failure::Semaphore:
        return "Semaphore";
    case Failure::Fence:
        return "Fence";
    }
    return "Unknown";
}

class SwapchainCreationTest : public testing::TestWithParam<Failure>
{
protected:
    void TearDown() override
    {
        // Parent identities are borrowed by this test; only the generation is created.
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectCapabilities(Failure failure)
    {
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(fakeVulkanHandle<VkPhysicalDevice>(2), surface, NotNull()))
            .WillOnce([failure](VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR* output) {
                *output = VkSurfaceCapabilitiesKHR {
                    .minImageCount = 2,
                    .maxImageCount = 0,
                    .currentExtent = {.width = 640, .height = 480},
                    .minImageExtent = {.width = 1, .height = 1},
                    .maxImageExtent = {.width = 8192, .height = 8192},
                    .maxImageArrayLayers = 1,
                    .supportedTransforms =
                        static_cast<VkSurfaceTransformFlagsKHR>(vk::SurfaceTransformFlagBitsKHR::eIdentity),
                    .currentTransform =
                        static_cast<VkSurfaceTransformFlagBitsKHR>(vk::SurfaceTransformFlagBitsKHR::eIdentity),
                    .supportedCompositeAlpha =
                        static_cast<VkCompositeAlphaFlagsKHR>(vk::CompositeAlphaFlagBitsKHR::eOpaque),
                    .supportedUsageFlags =
                        failure == Failure::Configuration
                            ? VkImageUsageFlags {}
                            : static_cast<VkImageUsageFlags>(vk::ImageUsageFlagBits::eColorAttachment)
                };
                return failure == Failure::Capabilities ? exhausted : success;
            });
    }

    void expectFormats(Failure failure)
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceSurfaceFormatsKHR(fakeVulkanHandle<VkPhysicalDevice>(2), surface, NotNull(), _))
            .Times(failure == Failure::Formats ? 1 : 2)
            .WillRepeatedly(
                [failure](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkSurfaceFormatKHR* output) {
                    *count = 1;
                    if (output != nullptr)
                    {
                        *output = VkSurfaceFormatKHR {
                            .format = static_cast<VkFormat>(vk::Format::eB8G8R8A8Srgb),
                            .colorSpace = static_cast<VkColorSpaceKHR>(vk::ColorSpaceKHR::eSrgbNonlinear)};
                    }
                    return failure == Failure::Formats ? exhausted : success;
                });
    }

    void expectSwapchain(Failure failure, bool separateQueues)
    {
        EXPECT_CALL(vulkan, vkCreateSwapchainKHR(fakeVulkanHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .WillOnce([this, failure, separateQueues](VkDevice,
                                                      const VkSwapchainCreateInfoKHR* info,
                                                      const VkAllocationCallbacks*,
                                                      VkSwapchainKHR* output) {
                EXPECT_EQ(info->surface, surface);
                EXPECT_EQ(info->oldSwapchain, oldSwapchain);
                EXPECT_EQ(info->minImageCount, 2U);
                EXPECT_EQ(info->imageExtent.width, 640U);
                EXPECT_EQ(info->presentMode, static_cast<VkPresentModeKHR>(vk::PresentModeKHR::eFifo));
                EXPECT_EQ(info->imageSharingMode,
                          static_cast<VkSharingMode>(separateQueues ? vk::SharingMode::eConcurrent
                                                                    : vk::SharingMode::eExclusive));
                EXPECT_EQ(info->queueFamilyIndexCount, separateQueues ? 2U : 0U);
                if (separateQueues && info->pQueueFamilyIndices != nullptr && info->queueFamilyIndexCount == 2)
                {
                    const auto families = std::span {info->pQueueFamilyIndices, info->queueFamilyIndexCount};
                    EXPECT_EQ(families.front(), 2U);
                    EXPECT_EQ(families.back(), 3U);
                }
                *output = chain;
                return failure == Failure::Swapchain ? exhausted : success;
            });
    }

    void expectImages(Failure failure, std::size_t count)
    {
        EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeVulkanHandle<VkDevice>(3), chain, NotNull(), _))
            .Times(failure == Failure::Images ? 1 : 2)
            .WillRepeatedly([failure, count](VkDevice, VkSwapchainKHR, std::uint32_t* size, VkImage* output) {
                *size = static_cast<std::uint32_t>(count);
                if (output != nullptr)
                {
                    const auto images = std::array {fakeVulkanHandle<VkImage>(10), fakeVulkanHandle<VkImage>(11)};
                    std::ranges::copy(std::span {images}.first(count), output);
                }
                return failure == Failure::Images ? exhausted : success;
            });
    }

    void expectView(std::size_t index, Failure failure)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(fakeVulkanHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce([index, failure](VkDevice,
                                       const VkImageViewCreateInfo* info,
                                       const VkAllocationCallbacks*,
                                       VkImageView* output) {
                EXPECT_EQ(info->image, fakeVulkanHandle<VkImage>(10 + index));
                EXPECT_EQ(info->viewType, static_cast<VkImageViewType>(vk::ImageViewType::e2D));
                EXPECT_EQ(info->format, static_cast<VkFormat>(vk::Format::eB8G8R8A8Srgb));
                EXPECT_EQ(info->subresourceRange.levelCount, 1U);
                EXPECT_EQ(info->subresourceRange.layerCount, 1U);
                *output = fakeVulkanHandle<VkImageView>(20 + index);
                return failure == Failure::View ? exhausted : success;
            });
    }

    void expectSemaphore(std::size_t index, Failure failure)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(fakeVulkanHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce(
                [index,
                 failure](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
                    *output = fakeVulkanHandle<VkSemaphore>(30 + index);
                    return failure == Failure::Semaphore ? exhausted : success;
                });
    }

    void expectFence(std::size_t index, Failure failure)
    {
        EXPECT_CALL(vulkan, vkCreateFence(fakeVulkanHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce(
                [index,
                 failure](VkDevice, const VkFenceCreateInfo* info, const VkAllocationCallbacks*, VkFence* output) {
                    EXPECT_EQ(info->flags, VkFenceCreateFlags {});
                    *output = fakeVulkanHandle<VkFence>(40 + index);
                    return failure == Failure::Fence ? exhausted : success;
                });
    }

    void expectImage(std::size_t index, Failure failure)
    {
        expectView(index, failure);
        if (failure != Failure::View)
        {
            expectSemaphore(index, failure);
            if (failure != Failure::Semaphore)
            {
                expectFence(index, failure);
            }
        }
        expectImageCleanup(index, failure);
    }

    void expectImageCleanup(std::size_t index, Failure failure)
    {
        if (failure != Failure::View && failure != Failure::Semaphore && failure != Failure::Fence)
        {
            cleanup += EXPECT_CALL(
                vulkan,
                vkDestroyFence(fakeVulkanHandle<VkDevice>(3), fakeVulkanHandle<VkFence>(40 + index), nullptr));
        }
        if (failure != Failure::View && failure != Failure::Semaphore)
        {
            cleanup += EXPECT_CALL(
                vulkan,
                vkDestroySemaphore(fakeVulkanHandle<VkDevice>(3), fakeVulkanHandle<VkSemaphore>(30 + index), nullptr));
        }
        if (failure != Failure::View)
        {
            cleanup += EXPECT_CALL(
                vulkan,
                vkDestroyImageView(fakeVulkanHandle<VkDevice>(3), fakeVulkanHandle<VkImageView>(20 + index), nullptr));
        }
    }

    void expectCreation(Failure failure, bool separateQueues = false, bool completedFirstImage = false)
    {
        expectCapabilities(failure);
        if (failure == Failure::Capabilities)
        {
            return;
        }
        expectFormats(failure);
        if (failure == Failure::Formats || failure == Failure::Configuration)
        {
            return;
        }
        expectSwapchain(failure, separateQueues);
        if (failure == Failure::Swapchain)
        {
            return;
        }
        expectOwnedImages(failure, completedFirstImage);
    }

    void expectOwnedImages(Failure failure, bool completedFirstImage)
    {
        expectImages(failure, 2U);
        if (failure != Failure::Images)
        {
            if (failure == Failure::None)
            {
                expectImage(0, Failure::None);
                expectImage(1, Failure::None);
            }
            else
            {
                if (completedFirstImage)
                {
                    expectImage(0, Failure::None);
                }
                expectImage(completedFirstImage ? 1U : 0U, failure);
            }
        }
        EXPECT_CALL(vulkan, vkDestroySwapchainKHR(fakeVulkanHandle<VkDevice>(3), chain, nullptr)).After(cleanup);
    }

    auto create(bool separateQueues = false) -> panda::Result<panda::detail::SwapchainGeneration>
    {
        return panda::detail::createSwapchainGeneration(
            *physicalDevice,
            device,
            surface,
            {.width = 320, .height = 240},
            vk::SwapchainKHR {oldSwapchain},
            {.name = {}, .apiVersion = 0, .queueFamily = 2, .presentQueueFamily = separateQueues ? 3U : 2U});
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physicalDevice {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physicalDevice, fakeVulkanHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeVulkanHandle<VkSurfaceKHR>(4)};
    VkSwapchainKHR chain {fakeVulkanHandle<VkSwapchainKHR>(5)};
    VkSwapchainKHR oldSwapchain {fakeVulkanHandle<VkSwapchainKHR>(6)};
    testing::Sequence creation;
    testing::ExpectationSet cleanup;
};
}

TEST_F(SwapchainCreationTest, OwnsCompleteGenerationAndKeepsSwapchainImagesBorrowed)
{
    expectCreation(Failure::None, false, true);
    const auto generation = create();
    ASSERT_TRUE(generation.has_value());
    ASSERT_EQ(generation->images.size(), 2U);
    EXPECT_EQ(*generation->swapchain, vk::SwapchainKHR {chain});
    EXPECT_EQ(generation->extent, (vk::Extent2D {.width = 640, .height = 480}));
    EXPECT_EQ(generation->images.front().image, vk::Image {fakeVulkanHandle<VkImage>(10)});
    EXPECT_EQ(*generation->images.front().view, vk::ImageView {fakeVulkanHandle<VkImageView>(20)});
}

TEST_F(SwapchainCreationTest, UsesConcurrentSharingForSeparateQueueFamilies)
{
    expectCreation(Failure::None, true);
    const auto generation = create(true);
    ASSERT_TRUE(generation.has_value());
}

TEST_P(SwapchainCreationTest, FailedCreationDoesNotPublishPartialGenerationOrDestroyFailedOutputs)
{
    expectCreation(GetParam());
    const auto generation = create();
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(
        generation.error().code,
        GetParam() == Failure::Configuration ? panda::ErrorCode::Unsupported : panda::ErrorCode::ResourceExhausted);
    if (GetParam() != Failure::Configuration)
    {
        ASSERT_TRUE(generation.error().native.has_value());
        EXPECT_EQ(generation.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
    }
}

INSTANTIATE_TEST_SUITE_P(CreationStages,
                         SwapchainCreationTest,
                         testing::Values(Failure::Capabilities,
                                         Failure::Formats,
                                         Failure::Configuration,
                                         Failure::Swapchain,
                                         Failure::Images,
                                         Failure::View,
                                         Failure::Semaphore,
                                         Failure::Fence),
                         failureName);

TEST_F(SwapchainCreationTest, LaterImageFailureReleasesCompletedImagesBeforeTheSwapchain)
{
    expectCreation(Failure::Fence, false, true);
    const auto generation = create();
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(generation.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(SwapchainCreationTest, FailedFormatFillIgnoresUndefinedOutputCount)
{
    expectCapabilities(Failure::None);
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceSurfaceFormatsKHR(fakeVulkanHandle<VkPhysicalDevice>(2), surface, NotNull(), _))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U), Return(success)))
        .WillOnce(
            testing::DoAll(testing::SetArgPointee<2>(std::numeric_limits<std::uint32_t>::max()), Return(exhausted)));
    const auto generation = create();
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(generation.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(SwapchainCreationTest, IncompleteImageEnumerationRetriesBeforeCreatingImageOwners)
{
    expectCapabilities(Failure::None);
    expectFormats(Failure::None);
    expectSwapchain(Failure::None, false);
    EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeVulkanHandle<VkDevice>(3), chain, NotNull(), testing::IsNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U), Return(success)))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(2U), Return(success)));
    EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeVulkanHandle<VkDevice>(3), chain, NotNull(), NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U),
                                 testing::SetArgPointee<3>(fakeVulkanHandle<VkImage>(10)),
                                 Return(static_cast<VkResult>(vk::Result::eIncomplete))))
        .WillOnce([](VkDevice, VkSwapchainKHR, std::uint32_t* count, VkImage* output) {
            *count = 2;
            std::ranges::copy(std::array {fakeVulkanHandle<VkImage>(10), fakeVulkanHandle<VkImage>(11)}, output);
            return success;
        });
    expectImage(0, Failure::None);
    expectImage(1, Failure::None);
    EXPECT_CALL(vulkan, vkDestroySwapchainKHR(fakeVulkanHandle<VkDevice>(3), chain, nullptr)).After(cleanup);
    const auto generation = create();
    ASSERT_TRUE(generation.has_value());
    EXPECT_EQ(generation->images.size(), 2U);
}

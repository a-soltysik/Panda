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
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <span>
#include <string>
#include <tuple>
#include <utility>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "presentation/SwapchainGeneration.hpp"

namespace
{
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

auto toVulkanPresentMode(vk::PresentModeKHR mode) noexcept -> VkPresentModeKHR
{
    return static_cast<VkPresentModeKHR>(mode);
}

auto requestedVulkanPresentMode(panda::EngineOptions::PresentationMode mode) noexcept -> vk::PresentModeKHR
{
    switch (mode)
    {
    case panda::EngineOptions::PresentationMode::Fifo:
        return vk::PresentModeKHR::eFifo;
    case panda::EngineOptions::PresentationMode::Mailbox:
        return vk::PresentModeKHR::eMailbox;
    case panda::EngineOptions::PresentationMode::Immediate:
        return vk::PresentModeKHR::eImmediate;
    }
    return vk::PresentModeKHR::eFifo;
}

enum class Failure : std::uint8_t
{
    None,
    Capabilities,
    Formats,
    Configuration,
    PresentModes,
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
    case Failure::PresentModes:
        return "PresentModes";
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
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(fakeHandle<VkPhysicalDevice>(2), surface, NotNull()))
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

    void expectFormats(Failure failure, vk::Format format = vk::Format::eB8G8R8A8Srgb)
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceSurfaceFormatsKHR(fakeHandle<VkPhysicalDevice>(2), surface, NotNull(), _))
            .Times(failure == Failure::Formats ? 1 : 2)
            .WillRepeatedly(
                [failure, format](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkSurfaceFormatKHR* output) {
                    *count = 1;
                    if (output != nullptr)
                    {
                        *output = VkSurfaceFormatKHR {
                            .format = static_cast<VkFormat>(format),
                            .colorSpace = static_cast<VkColorSpaceKHR>(vk::ColorSpaceKHR::eSrgbNonlinear)};
                    }
                    return failure == Failure::Formats ? exhausted : success;
                });
    }

    void expectPresentModes(Failure failure, bool fifoOnly = false)
    {
        const auto modes =
            std::array {vk::PresentModeKHR::eFifo, vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate};
        const auto modeCount = fifoOnly ? 1U : static_cast<std::uint32_t>(modes.size());
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceSurfacePresentModesKHR(fakeHandle<VkPhysicalDevice>(2), surface, NotNull(), _))
            .Times(2)
            .WillRepeatedly([failure, modes, modeCount](VkPhysicalDevice,
                                                        VkSurfaceKHR,
                                                        std::uint32_t* count,
                                                        VkPresentModeKHR* output) noexcept {
                *count = modeCount;
                if (output != nullptr)
                {
                    std::ranges::transform(std::span {modes}.first(modeCount), output, toVulkanPresentMode);
                    return failure == Failure::PresentModes ? exhausted : success;
                }
                return success;
            });
    }

    void verifySwapchainIdentity(const VkSwapchainCreateInfoKHR& info, vk::PresentModeKHR presentMode)
    {
        EXPECT_EQ(info.surface, surface);
        EXPECT_EQ(info.oldSwapchain, oldSwapchain);
        EXPECT_EQ(info.minImageCount, 2U);
        EXPECT_EQ(info.imageExtent.width, 640U);
        EXPECT_EQ(info.presentMode, static_cast<VkPresentModeKHR>(presentMode));
    }

    static void verifySwapchainSharing(const VkSwapchainCreateInfoKHR& info, bool separateQueues)
    {
        EXPECT_EQ(
            info.imageSharingMode,
            static_cast<VkSharingMode>(separateQueues ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive));
        EXPECT_EQ(info.queueFamilyIndexCount, separateQueues ? 2U : 0U);
        if (separateQueues)
        {
            ASSERT_NE(info.pQueueFamilyIndices, nullptr);
            ASSERT_EQ(info.queueFamilyIndexCount, 2U);
            const auto families = std::span {info.pQueueFamilyIndices, info.queueFamilyIndexCount};
            EXPECT_EQ(families.front(), 2U);
            EXPECT_EQ(families.back(), 3U);
        }
    }

    void verifySwapchainInfo(const VkSwapchainCreateInfoKHR& info,
                             bool separateQueues,
                             vk::PresentModeKHR presentMode,
                             vk::Format format)
    {
        verifySwapchainIdentity(info, presentMode);
        verifySwapchainSharing(info, separateQueues);
        EXPECT_EQ(info.imageFormat, static_cast<VkFormat>(format));
    }

    void expectSwapchain(Failure failure,
                         bool separateQueues,
                         vk::PresentModeKHR presentMode,
                         vk::Format format = vk::Format::eB8G8R8A8Srgb)
    {
        EXPECT_CALL(vulkan, vkCreateSwapchainKHR(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .WillOnce([this, failure, separateQueues, presentMode, format](VkDevice,
                                                                           const VkSwapchainCreateInfoKHR* info,
                                                                           const VkAllocationCallbacks*,
                                                                           VkSwapchainKHR* output) {
                verifySwapchainInfo(*info, separateQueues, presentMode, format);
                *output = chain;
                return failure == Failure::Swapchain ? exhausted : success;
            });
    }

    void expectImages(Failure failure, std::size_t count)
    {
        EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeHandle<VkDevice>(3), chain, NotNull(), _))
            .Times(failure == Failure::Images ? 1 : 2)
            .WillRepeatedly([failure, count](VkDevice, VkSwapchainKHR, std::uint32_t* size, VkImage* output) {
                *size = static_cast<std::uint32_t>(count);
                if (output != nullptr)
                {
                    const auto images = std::array {fakeHandle<VkImage>(10), fakeHandle<VkImage>(11)};
                    std::ranges::copy(std::span {images}.first(count), output);
                }
                return failure == Failure::Images ? exhausted : success;
            });
    }

    void expectView(std::size_t index, Failure failure, vk::Format format = vk::Format::eB8G8R8A8Srgb)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce([index, failure, format](VkDevice,
                                               const VkImageViewCreateInfo* info,
                                               const VkAllocationCallbacks*,
                                               VkImageView* output) {
                EXPECT_EQ(info->image, fakeHandle<VkImage>(10 + index));
                EXPECT_EQ(info->viewType, static_cast<VkImageViewType>(vk::ImageViewType::e2D));
                EXPECT_EQ(info->format, static_cast<VkFormat>(format));
                EXPECT_EQ(info->subresourceRange.levelCount, 1U);
                EXPECT_EQ(info->subresourceRange.layerCount, 1U);
                *output = fakeHandle<VkImageView>(20 + index);
                return failure == Failure::View ? exhausted : success;
            });
    }

    void expectSemaphore(std::size_t index, Failure failure)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce(
                [index,
                 failure](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
                    *output = fakeHandle<VkSemaphore>(30 + index);
                    return failure == Failure::Semaphore ? exhausted : success;
                });
    }

    void expectFence(std::size_t index, Failure failure)
    {
        EXPECT_CALL(vulkan, vkCreateFence(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce(
                [index,
                 failure](VkDevice, const VkFenceCreateInfo* info, const VkAllocationCallbacks*, VkFence* output) {
                    EXPECT_EQ(info->flags, VkFenceCreateFlags {});
                    *output = fakeHandle<VkFence>(40 + index);
                    return failure == Failure::Fence ? exhausted : success;
                });
    }

    void expectImage(std::size_t index, Failure failure, vk::Format format = vk::Format::eB8G8R8A8Srgb)
    {
        expectView(index, failure, format);
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
            cleanup +=
                EXPECT_CALL(vulkan, vkDestroyFence(fakeHandle<VkDevice>(3), fakeHandle<VkFence>(40 + index), nullptr));
        }
        if (failure != Failure::View && failure != Failure::Semaphore)
        {
            cleanup +=
                EXPECT_CALL(vulkan,
                            vkDestroySemaphore(fakeHandle<VkDevice>(3), fakeHandle<VkSemaphore>(30 + index), nullptr));
        }
        if (failure != Failure::View)
        {
            cleanup +=
                EXPECT_CALL(vulkan,
                            vkDestroyImageView(fakeHandle<VkDevice>(3), fakeHandle<VkImageView>(20 + index), nullptr));
        }
    }

    void expectCreation(
        Failure failure,
        bool separateQueues = false,
        bool completedFirstImage = false,
        panda::EngineOptions::PresentationMode presentationMode = panda::EngineOptions::PresentationMode::Fifo,
        vk::Format format = vk::Format::eB8G8R8A8Srgb)
    {
        expectCapabilities(failure);
        if (failure == Failure::Capabilities)
        {
            return;
        }
        expectFormats(failure, format);
        if (failure == Failure::Formats || failure == Failure::Configuration)
        {
            return;
        }
        expectPresentModes(failure);
        if (failure == Failure::PresentModes)
        {
            return;
        }
        const auto vulkanMode = requestedVulkanPresentMode(presentationMode);
        expectSwapchain(failure, separateQueues, vulkanMode, format);
        if (failure == Failure::Swapchain)
        {
            return;
        }
        expectOwnedImages(failure, completedFirstImage, format);
    }

    void expectOwnedImages(Failure failure, bool completedFirstImage, vk::Format format)
    {
        expectImages(failure, 2U);
        if (failure == Failure::Images)
        {
            EXPECT_CALL(vulkan, vkDestroySwapchainKHR(fakeHandle<VkDevice>(3), chain, nullptr)).After(cleanup);
            return;
        }
        expectImageFailures(failure, completedFirstImage, format);
        EXPECT_CALL(vulkan, vkDestroySwapchainKHR(fakeHandle<VkDevice>(3), chain, nullptr)).After(cleanup);
    }

    void expectImageFailures(Failure failure, bool completedFirstImage, vk::Format format)
    {
        if (failure == Failure::None)
        {
            expectImage(0, Failure::None, format);
            expectImage(1, Failure::None, format);
            return;
        }
        if (completedFirstImage)
        {
            expectImage(0, Failure::None, format);
        }
        expectImage(completedFirstImage ? 1U : 0U, failure, format);
    }

    auto create(bool separateQueues = false,
                panda::EngineOptions::PresentationMode presentationMode = panda::EngineOptions::PresentationMode::Fifo)
        -> panda::core::vulkan::SwapchainGenerationResult
    {
        return panda::core::vulkan::createSwapchainGeneration({
            .physicalDevice = *physicalDevice,
            .device = device,
            .surface = surface,
            .requested = {320, 240},
            .oldSwapchain = vk::SwapchainKHR {oldSwapchain},
            .queueFamily = 2,
            .presentQueueFamily = separateQueues ? 3U : 2U,
            .presentationMode = presentationMode
        });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physicalDevice {instance, fakeHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physicalDevice, fakeHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeHandle<VkSurfaceKHR>(4)};
    VkSwapchainKHR chain {fakeHandle<VkSwapchainKHR>(5)};
    VkSwapchainKHR oldSwapchain {fakeHandle<VkSwapchainKHR>(6)};
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
    EXPECT_EQ(generation->format, vk::Format::eB8G8R8A8Srgb);
    EXPECT_EQ(generation->images.front().image, vk::Image {fakeHandle<VkImage>(10)});
    EXPECT_EQ(*generation->images.front().view, vk::ImageView {fakeHandle<VkImageView>(20)});
}

TEST_F(SwapchainCreationTest, PreservesTheSelectedRgbaSrgbSurfaceFormat)
{
    expectCreation(Failure::None, false, true, panda::EngineOptions::PresentationMode::Fifo, vk::Format::eR8G8B8A8Srgb);
    const auto generation = create();
    ASSERT_TRUE(generation);
    EXPECT_EQ(generation->format, vk::Format::eR8G8B8A8Srgb);
}

TEST_F(SwapchainCreationTest, UsesConcurrentSharingForSeparateQueueFamilies)
{
    expectCreation(Failure::None, true);
    const auto generation = create(true);
    ASSERT_TRUE(generation.has_value());
}

TEST_F(SwapchainCreationTest, UsesRequestedMailboxMode)
{
    using Mode = panda::EngineOptions::PresentationMode;
    expectCreation(Failure::None, false, false, Mode::Mailbox);
    const auto generation = create(false, Mode::Mailbox);
    ASSERT_TRUE(generation.has_value());
}

TEST_F(SwapchainCreationTest, UsesRequestedImmediateMode)
{
    using Mode = panda::EngineOptions::PresentationMode;
    expectCreation(Failure::None, false, false, Mode::Immediate);
    const auto generation = create(false, Mode::Immediate);
    ASSERT_TRUE(generation.has_value());
}

TEST_F(SwapchainCreationTest, RejectsUnsupportedRequestedModeBeforeCreatingSwapchain)
{
    using Mode = panda::EngineOptions::PresentationMode;
    expectCapabilities(Failure::None);
    expectFormats(Failure::None);
    expectPresentModes(Failure::None, true);
    EXPECT_CALL(vulkan, vkCreateSwapchainKHR(_, _, _, _)).Times(0);

    const auto generation = create(false, Mode::Mailbox);
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(generation.error().cause.code, panda::ErrorCode::Unsupported);
    EXPECT_NE(generation.error().cause.message.find("MAILBOX"), std::string::npos);
    EXPECT_FALSE(generation.error().oldSwapchainRetired);
}

TEST_P(SwapchainCreationTest, FailedCreationDoesNotPublishPartialGenerationOrDestroyFailedOutputs)
{
    expectCreation(GetParam());
    const auto generation = create();
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(generation.error().oldSwapchainRetired, GetParam() >= Failure::Swapchain);
    EXPECT_EQ(
        generation.error().cause.code,
        GetParam() == Failure::Configuration ? panda::ErrorCode::Unsupported : panda::ErrorCode::ResourceExhausted);
    if (GetParam() != Failure::Configuration)
    {
        ASSERT_TRUE(generation.error().cause.native.has_value());
        EXPECT_EQ(generation.error().cause.native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
    }
}

INSTANTIATE_TEST_SUITE_P(CreationStages,
                         SwapchainCreationTest,
                         testing::Values(Failure::Capabilities,
                                         Failure::Formats,
                                         Failure::Configuration,
                                         Failure::PresentModes,
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
    EXPECT_EQ(generation.error().cause.code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(SwapchainCreationTest, FailedFormatFillIgnoresUndefinedOutputCount)
{
    expectCapabilities(Failure::None);
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceFormatsKHR(fakeHandle<VkPhysicalDevice>(2), surface, NotNull(), _))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U), Return(success)))
        .WillOnce(
            testing::DoAll(testing::SetArgPointee<2>(std::numeric_limits<std::uint32_t>::max()), Return(exhausted)));
    const auto generation = create();
    ASSERT_FALSE(generation.has_value());
    EXPECT_EQ(generation.error().cause.code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(SwapchainCreationTest, IncompleteImageEnumerationRetriesBeforeCreatingImageOwners)
{
    expectCapabilities(Failure::None);
    expectFormats(Failure::None);
    expectPresentModes(Failure::None);
    expectSwapchain(Failure::None, false, vk::PresentModeKHR::eFifo);
    EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeHandle<VkDevice>(3), chain, NotNull(), testing::IsNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U), Return(success)))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(2U), Return(success)));
    EXPECT_CALL(vulkan, vkGetSwapchainImagesKHR(fakeHandle<VkDevice>(3), chain, NotNull(), NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<2>(1U),
                                 testing::SetArgPointee<3>(fakeHandle<VkImage>(10)),
                                 Return(static_cast<VkResult>(vk::Result::eIncomplete))))
        .WillOnce([](VkDevice, VkSwapchainKHR, std::uint32_t* count, VkImage* output) {
            *count = 2;
            std::ranges::copy(std::array {fakeHandle<VkImage>(10), fakeHandle<VkImage>(11)}, output);
            return success;
        });
    expectImage(0, Failure::None);
    expectImage(1, Failure::None);
    EXPECT_CALL(vulkan, vkDestroySwapchainKHR(fakeHandle<VkDevice>(3), chain, nullptr)).After(cleanup);
    const auto generation = create();
    ASSERT_TRUE(generation.has_value());
    EXPECT_EQ(generation->images.size(), 2U);
}

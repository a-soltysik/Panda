
// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "Presentation.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;
using testing::Sequence;
using testing::StrEq;

auto releaseProcAddress() -> PFN_vkVoidFunction
{
    // Vulkan specifies this loader-provided function-pointer conversion.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<PFN_vkVoidFunction>(&vkReleaseSwapchainImagesKHR);
}

void expectImageLayouts(const VkImageMemoryBarrier2& barrier, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    EXPECT_EQ(barrier.oldLayout, oldLayout);
    EXPECT_EQ(barrier.newLayout, newLayout);
}

void expectTransitionToColorAttachment(const VkDependencyInfo* dependency)
{
    ASSERT_EQ(dependency->imageMemoryBarrierCount, 1U);
    ASSERT_NE(dependency->pImageMemoryBarriers, nullptr);
    const auto& barrier = *dependency->pImageMemoryBarriers;
    EXPECT_EQ(barrier.srcStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.srcAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eNone));
    EXPECT_EQ(barrier.dstStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.dstAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eColorAttachmentWrite));
    expectImageLayouts(barrier,
                       static_cast<VkImageLayout>(vk::ImageLayout::eUndefined),
                       static_cast<VkImageLayout>(vk::ImageLayout::eColorAttachmentOptimal));
}

void expectTransitionToPresent(const VkDependencyInfo* dependency)
{
    ASSERT_EQ(dependency->imageMemoryBarrierCount, 1U);
    ASSERT_NE(dependency->pImageMemoryBarriers, nullptr);
    const auto& barrier = *dependency->pImageMemoryBarriers;
    EXPECT_EQ(barrier.srcStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.srcAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eColorAttachmentWrite));
    EXPECT_EQ(barrier.dstStageMask, static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eNone));
    EXPECT_EQ(barrier.dstAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eNone));
    expectImageLayouts(barrier,
                       static_cast<VkImageLayout>(vk::ImageLayout::eColorAttachmentOptimal),
                       static_cast<VkImageLayout>(vk::ImageLayout::ePresentSrcKHR));
}

auto makeGeneration(const vk::raii::Device& device, std::uintptr_t handle)
    -> std::expected<panda::detail::SwapchainGeneration, panda::Error>
{
    auto images = std::vector<panda::detail::SwapchainImage> {};
    images.emplace_back(panda::detail::SwapchainImage {
        .image = vk::Image {fakeVulkanHandle<VkImage>(handle + 1U)},
        .view = {nullptr},
        .renderFinished = vk::raii::Semaphore {device, fakeVulkanHandle<VkSemaphore>(handle + 2U)},
        .presentFence = vk::raii::Fence {device, fakeVulkanHandle<VkFence>(handle + 3U)},
        .presentPending = false,
        .initialized = false
    });
    return panda::detail::SwapchainGeneration {
        .swapchain = vk::raii::SwapchainKHR {device,       fakeVulkanHandle<VkSwapchainKHR>(handle)},
        .images = std::move(images),
        .extent = vk::Extent2D {.width = 640, .height = 480                           },
        .format = vk::Format::eB8G8R8A8Srgb
    };
}

class PresentationTest : public testing::Test
{
protected:
    std::uint64_t maximumDifference {std::numeric_limits<std::uint64_t>::max()};

    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                auto* timeline = static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(properties->pNext);
                ASSERT_NE(timeline, nullptr);
                timeline->maxTimelineSemaphoreValueDifference = maximumDifference;
            });
    }

    void TearDown() override
    {
        // These adopted tokens represent borrowed test identities, not created objects.
        if (_device)
        {
            std::ignore = _device->release();
        }
        std::ignore = _instance.release();
    }

    auto getDevice() -> const vk::raii::Device&
    {
        if (!_device)
        {
            const auto physicalDevice = vk::raii::PhysicalDevice {_instance, fakeVulkanHandle<VkPhysicalDevice>(1)};
            _device.emplace(physicalDevice, fakeVulkanHandle<VkDevice>(1));
        }
        return *_device;
    }

    struct ResourceDestructionCounts
    {
        std::size_t semaphores {};
        std::size_t swapchains {};
    };

    void expectQueueLookup()
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(_, _, _, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkDevice, std::uint32_t, std::uint32_t, VkQueue* queue) {
                *queue = VkQueue {};
            });
    }

    void expectReleaseEntryPoint()
    {
        EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesKHR")))
            .Times(1)
            .WillRepeatedly([](VkDevice, const char*) {
                return releaseProcAddress();
            });
    }

    void expectFrameResources(std::size_t semaphoreCount)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
            .Times(static_cast<int>(semaphoreCount))
            .WillRepeatedly(
                [this](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* out) {
                    *out = fakeVulkanHandle<VkSemaphore>(_nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
            .Times(2)
            .WillRepeatedly(
                [this](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* out) {
                    *out = fakeVulkanHandle<VkCommandPool>(_nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull()))
            .Times(2)
            .WillRepeatedly([this](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* out) {
                *out = fakeVulkanHandle<VkCommandBuffer>(_nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void expectResourceDestruction(ResourceDestructionCounts counts)
    {
        EXPECT_CALL(vulkan, vkDestroySemaphore(_, _, _)).Times(static_cast<int>(counts.semaphores));
        EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, _)).Times(2);
        EXPECT_CALL(vulkan, vkDestroySwapchainKHR(_, _, _)).Times(static_cast<int>(counts.swapchains));
        EXPECT_CALL(vulkan, vkDestroyFence(_, _, _)).Times(static_cast<int>(counts.swapchains));
    }

    void expectExtentReads(std::size_t count) const
    {
        EXPECT_CALL(window, getFramebufferExtent()).Times(static_cast<int>(count)).WillRepeatedly([] {
            return panda::test::WindowSurfaceMock::ExtentResult {
                panda::FramebufferExtent {.width = 640, .height = 480}
            };
        });
    }

    void expectInitialGeneration(std::uintptr_t handle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .WillOnce([handle](const panda::detail::SwapchainGenerationRequest& request) {
                EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {});
                return makeGeneration(request.device, handle);
            });
    }

    void expectWindowFrame()
    {
        expectQueueLookup();
        expectReleaseEntryPoint();
        expectFrameResources(3);
        expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
        expectExtentReads(2);
        expectInitialGeneration(700);
    }

    void expectAcquiredImage(Sequence& flow, vk::Result result)
    {
        EXPECT_CALL(vulkan, vkAcquireNextImageKHR(_, _, _, _, _, NotNull()))
            .InSequence(flow)
            .WillOnce([result](VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t* index) {
                *index = 0;
                return static_cast<VkResult>(result);
            });
    }

    void expectCommandPoolResetFailure(Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    }

    void expectCommandRecording(Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce([](VkCommandBuffer, const VkDependencyInfo* dependency) {
                expectTransitionToColorAttachment(dependency);
            });
        EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull())).InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdEndRendering(_)).InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce([](VkCommandBuffer, const VkDependencyInfo* dependency) {
                expectTransitionToPresent(dependency);
            });
        EXPECT_CALL(vulkan, vkEndCommandBuffer(_))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    }

    void expectRetirementSubmit(Sequence& flow, vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(_, 1, NotNull(), _))
            .InSequence(flow)
            .WillOnce([result](VkQueue, std::uint32_t, const VkSubmitInfo2* submits, VkFence) {
                EXPECT_EQ(submits->waitSemaphoreInfoCount, 1U);
                EXPECT_EQ(submits->commandBufferInfoCount, 0U);
                EXPECT_EQ(submits->signalSemaphoreInfoCount, 1U);
                if (submits->signalSemaphoreInfoCount != 1U || submits->pSignalSemaphoreInfos == nullptr)
                {
                    ADD_FAILURE() << "Retirement submit must signal one timeline semaphore";
                    return static_cast<VkResult>(vk::Result::eErrorUnknown);
                }
                EXPECT_EQ(submits->pSignalSemaphoreInfos->value, 1U);
                return static_cast<VkResult>(result);
            });
    }

    void expectFrameSubmit(Sequence& flow, vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(_, 1, NotNull(), _))
            .InSequence(flow)
            .WillOnce([result](VkQueue, std::uint32_t, const VkSubmitInfo2* submits, VkFence) {
                EXPECT_EQ(submits->waitSemaphoreInfoCount, 1U);
                if (submits->waitSemaphoreInfoCount != 1U || submits->pWaitSemaphoreInfos == nullptr ||
                    submits->signalSemaphoreInfoCount != 2U || submits->pSignalSemaphoreInfos == nullptr)
                {
                    ADD_FAILURE() << "Frame submit must wait once and signal twice";
                    return static_cast<VkResult>(vk::Result::eErrorUnknown);
                }
                const auto signals = std::span {submits->pSignalSemaphoreInfos, submits->signalSemaphoreInfoCount};
                EXPECT_EQ(submits->pWaitSemaphoreInfos->stageMask,
                          static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands));
                EXPECT_EQ(submits->commandBufferInfoCount, 1U);
                EXPECT_EQ(signals.subspan(1).front().value, 1U);
                return static_cast<VkResult>(result);
            });
    }

    void expectPresentFailure(Sequence& flow, vk::Result result = vk::Result::eErrorUnknown)
    {
        EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(result)));
    }

    void expectPendingCounter(Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkGetSemaphoreCounterValue(_, _, NotNull()))
            .InSequence(flow)
            .WillOnce([](VkDevice, VkSemaphore, std::uint64_t* output) {
                *output = 0;
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void expectTimelineWait(Sequence& flow, vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(vulkan, vkWaitSemaphores(_, NotNull(), _))
            .InSequence(flow)
            .WillOnce([result](VkDevice, const VkSemaphoreWaitInfo* waitInfo, std::uint64_t) {
                EXPECT_EQ(waitInfo->semaphoreCount, 1U);
                if (waitInfo->semaphoreCount != 1U || waitInfo->pValues == nullptr)
                {
                    ADD_FAILURE() << "Timeline wait must contain one value";
                    return static_cast<VkResult>(vk::Result::eErrorUnknown);
                }
                EXPECT_EQ(*waitInfo->pValues, 1U);
                return static_cast<VkResult>(result);
            });
    }

    void expectImageRelease(const Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkReleaseSwapchainImagesKHR(_, NotNull()))
            .InSequence(flow)
            .WillOnce([](VkDevice, const VkReleaseSwapchainImagesInfoKHR* releaseInfo) {
                EXPECT_EQ(releaseInfo->imageIndexCount, 1U);
                if (releaseInfo->imageIndexCount != 1U || releaseInfo->pImageIndices == nullptr)
                {
                    ADD_FAILURE() << "Image release must contain one index";
                    return static_cast<VkResult>(vk::Result::eErrorUnknown);
                }
                EXPECT_EQ(*releaseInfo->pImageIndices, 0U);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void expectFailedRecreation(const Sequence& flow, std::uintptr_t oldHandle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .InSequence(flow)
            .WillOnce([oldHandle](const panda::detail::SwapchainGenerationRequest& request)
                          -> std::expected<panda::detail::SwapchainGeneration, panda::Error> {
                EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {fakeVulkanHandle<VkSwapchainKHR>(oldHandle)});
                return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "replacement failed")};
            });
    }

    void expectRetriedGeneration(Sequence& flow, std::uintptr_t handle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .InSequence(flow)
            .WillOnce([handle](const panda::detail::SwapchainGenerationRequest& request) {
                EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {});
                return makeGeneration(request.device, handle);
            });
    }

    auto createPresentation(panda::ContextDeviceInfo::SwapchainMaintenance maintenance =
                                panda::ContextDeviceInfo::SwapchainMaintenance::Khr)
        -> std::expected<std::unique_ptr<panda::detail::Presentation>, panda::Error>
    {
        return panda::detail::Presentation::create({
            .physicalDevice = vk::PhysicalDevice {},
            .device = getDevice(),
            .surface = vk::SurfaceKHR {},
            .window = window,
            .deviceInfo = {.name = {},
                                                  .apiVersion = 0,
                                                  .queueFamily = 0,
                                                  .presentQueueFamily = 0,
                                                  .swapchainMaintenance = maintenance}
        });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    testing::StrictMock<panda::test::WindowSurfaceMock> window;
    panda::test::ScopedMock<panda::test::SwapchainGenerationMock> swapchains;

private:
    vk::raii::Context _loader {vkGetInstanceProcAddr};
    vk::raii::Instance _instance {_loader, fakeVulkanHandle<VkInstance>(1)};
    std::optional<vk::raii::Device> _device;
    std::uintptr_t _nextHandle {100};
};
}

TEST_F(PresentationTest, ReportsUnavailableKhrReleaseFunction)
{
    expectQueueLookup();
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesKHR"))).WillOnce(Return(nullptr));
    const auto presentation = createPresentation();
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(presentation.error().message.find("VK_KHR_swapchain_maintenance1"), std::string::npos);
}

TEST_F(PresentationTest, ReportsUnavailableExtReleaseFunction)
{
    expectQueueLookup();
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesEXT"))).WillOnce(Return(nullptr));
    const auto presentation = createPresentation(panda::ContextDeviceInfo::SwapchainMaintenance::Ext);
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(presentation.error().message.find("VK_EXT_swapchain_maintenance1"), std::string::npos);
}

TEST_F(PresentationTest, PreservesTimelineSemaphoreCreationFailure)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
            *output = fakeVulkanHandle<VkSemaphore>(2);
            return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
        });
    EXPECT_CALL(vulkan, vkDestroySemaphore(_, _, _)).Times(0);
    const auto presentation = createPresentation();
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(presentation.error().native.has_value());
    const auto native = presentation.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "Vulkan");
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationTest, FailedCommandPoolCreationReleasesOnlyTheCreatedSemaphore)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
            *output = fakeVulkanHandle<VkSemaphore>(2);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* output) {
            *output = fakeVulkanHandle<VkCommandPool>(3);
            return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
        });
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, _)).Times(0);
    EXPECT_CALL(vulkan, vkDestroySemaphore(_, fakeVulkanHandle<VkSemaphore>(2), nullptr));

    const auto presentation = createPresentation();

    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(PresentationTest, RetiresAcquiredImageWhenCommandRecordingCannotStart)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
    expectExtentReads(2);
    expectInitialGeneration(500);
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation.has_value());
    const auto result = presentation->get()->presentClearFrame();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(result.error().native.has_value());
    const auto native = result.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationTest, PresentAndRecreationFailuresReleaseThenRetryWithoutRetiredSwapchain)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(4);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 6, .swapchains = 2});
    expectExtentReads(4);
    expectInitialGeneration(700);
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));

    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow);
    expectPresentFailure(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);
    expectFailedRecreation(flow, 700);
    expectRetriedGeneration(flow, 900);
    expectAcquiredImage(flow, vk::Result::eTimeout);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation.has_value());
    const auto present = presentation->get()->presentClearFrame();
    ASSERT_FALSE(present.has_value());
    EXPECT_EQ(present.error().code, panda::ErrorCode::BackendFailure);
    const auto failedRecreation = presentation->get()->presentClearFrame();
    ASSERT_FALSE(failedRecreation.has_value());
    EXPECT_EQ(failedRecreation.error().message, "replacement failed");
    const auto retried = presentation->get()->presentClearFrame();
    ASSERT_TRUE(retried.has_value());
    EXPECT_EQ(retried->status, panda::FrameResult::Status::Skipped);
}

TEST_F(PresentationTest, TimelineLimitRejectsFrameBeforeAcquiringAnotherImage)
{
    maximumDifference = 1;
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
    expectExtentReads(3);
    expectInitialGeneration(700);
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow);
    EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    expectPendingCounter(flow);
    expectTimelineWait(flow);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto first = presentation->get()->presentClearFrame();
    ASSERT_TRUE(first);
    const auto blocked = presentation->get()->presentClearFrame();
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationTest, FailedFrameSubmitRetiresAcquireWithoutConsumingTheCandidateValue)
{
    expectWindowFrame();
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow, vk::Result::eErrorOutOfDeviceMemory);
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->presentClearFrame();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(PresentationTest, DeviceLostSubmitDoesNotRetryAcquireRetirement)
{
    expectWindowFrame();
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow, vk::Result::eErrorDeviceLost);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->presentClearFrame();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_FALSE(presentation->get()->presentClearFrame());
}

TEST_F(PresentationTest, FailedRetirementSubmitPreservesErrorWithoutWaitingOnUncommittedWork)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow, vk::Result::eErrorOutOfDeviceMemory);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->presentClearFrame();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_FALSE(presentation->get()->presentClearFrame());
    EXPECT_FALSE(presentation->get()->drain());
}

TEST_F(PresentationTest, RetirementTimeoutKeepsTheCommittedWorkForDrain)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow);
    expectTimelineWait(flow, vk::Result::eTimeout);
    expectTimelineWait(flow);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->presentClearFrame();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::Timeout);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationTest, OutOfDateAcquireReturnsNoCompletionPoint)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eErrorOutOfDateKHR);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto frame = presentation->get()->presentClearFrame();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_FALSE(frame->completion.isValid());
}

TEST_F(PresentationTest, SuboptimalPresentReturnsTheCommittedCompletionPoint)
{
    expectWindowFrame();
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow);
    expectPresentFailure(flow, vk::Result::eSuboptimalKHR);
    expectTimelineWait(flow);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto frame = presentation->get()->presentClearFrame();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_TRUE(frame->completion.isValid());
    EXPECT_TRUE(presentation->get()->drain());
}

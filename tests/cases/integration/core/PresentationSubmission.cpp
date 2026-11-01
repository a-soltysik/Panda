
// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

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
#include <panda/RenderView.hpp>
#include <span>
#include <string>

#include "PresentationTestSupport.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "presentation/Presentation.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;
using testing::Sequence;
using testing::StrEq;

class PresentationSubmissionIntegration : public panda::test::PresentationTestSupport
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

    void expectFrameResources(std::size_t count)
    {
        expectNativeFrameResources(count);
    }

    void expectResourceDestruction(ResourceDestructionCounts counts)
    {
        expectNativeResourceDestruction(counts);
    }

    void TearDown() override
    {
        _submissions.reset();
        panda::test::PresentationTestSupport::TearDown();
    }

    void expectQueueLookup()
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(_, _, _, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkDevice, std::uint32_t, std::uint32_t, VkQueue* queue) {
                *queue = VkQueue {};
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

    auto createPresentation(panda::ContextDeviceInfo::SwapchainMaintenance maintenance =
                                panda::ContextDeviceInfo::SwapchainMaintenance::Khr)
        -> std::expected<std::unique_ptr<panda::core::vulkan::Presentation>, panda::Error>
    {
        return panda::core::vulkan::Presentation::create({
            .physicalDevice = vk::PhysicalDevice {},
            .device = getDevice(),
            .surface = vk::SurfaceKHR {},
            .window = window,
            .deviceInfo = {.name = {},
                                                  .driverName = {},
                                                  .driverInfo = {},
                                                  .driverVersion = 0,
                                                  .apiVersion = 0,
                                                  .queueFamily = 0,
                                                  .presentQueueFamily = 0,
                                                  .swapchainMaintenance = maintenance},
            .submissions = _submissions.emplace(getDevice(), 0)
        });
    }

private:
    std::optional<panda::detail::SubmissionQueue> _submissions;
};
}

TEST_F(PresentationSubmissionIntegration, ReportsUnavailableKhrReleaseFunction)
{
    expectQueueLookup();
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesKHR"))).WillOnce(Return(nullptr));
    const auto presentation = createPresentation();
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(presentation.error().message.find("VK_KHR_swapchain_maintenance1"), std::string::npos);
}

TEST_F(PresentationSubmissionIntegration, ReportsUnavailableExtReleaseFunction)
{
    expectQueueLookup();
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesEXT"))).WillOnce(Return(nullptr));
    const auto presentation = createPresentation(panda::ContextDeviceInfo::SwapchainMaintenance::Ext);
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(presentation.error().message.find("VK_EXT_swapchain_maintenance1"), std::string::npos);
}

TEST_F(PresentationSubmissionIntegration, PreservesTimelineSemaphoreCreationFailure)
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

TEST_F(PresentationSubmissionIntegration, FailedCommandPoolCreationReleasesOnlyTheCreatedSemaphore)
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

TEST_F(PresentationSubmissionIntegration, RetiresAcquiredImageWhenCommandRecordingCannotStart)
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
    const auto result = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(result.error().native.has_value());
    const auto native = result.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationSubmissionIntegration, PresentAndRecreationFailuresReleaseThenRetryWithoutRetiredSwapchain)
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
    const auto present = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(present.has_value());
    EXPECT_EQ(present.error().code, panda::ErrorCode::BackendFailure);
    const auto failedRecreation = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failedRecreation.has_value());
    EXPECT_EQ(failedRecreation.error().message, "replacement failed");
    const auto retried = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(retried.has_value());
    EXPECT_EQ(retried->status, panda::FrameResult::Status::Skipped);
}

TEST_F(PresentationSubmissionIntegration, TimelineLimitRejectsFrameBeforeAcquiringAnotherImage)
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
    const auto first = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(first);
    const auto blocked = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationSubmissionIntegration, FailedFrameSubmitRetiresAcquireWithoutConsumingTheCandidateValue)
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
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(PresentationSubmissionIntegration, DeviceLostSubmitDoesNotRetryAcquireRetirement)
{
    expectWindowFrame();
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow, vk::Result::eErrorDeviceLost);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
}

TEST_F(PresentationSubmissionIntegration, FailedRetirementSubmitPreservesErrorWithoutWaitingOnUncommittedWork)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow, vk::Result::eErrorOutOfDeviceMemory);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
    EXPECT_FALSE(presentation->get()->drain());
}

TEST_F(PresentationSubmissionIntegration, RetirementTimeoutDrainReleasesTheAcquiredImage)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow);
    expectTimelineWait(flow, vk::Result::eTimeout);
    expectTimelineWait(flow);
    expectImageRelease(flow);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::Timeout);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationSubmissionIntegration, OutOfDateAcquireReturnsNoCompletionPoint)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eErrorOutOfDateKHR);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto frame = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_FALSE(frame->completion.isValid());
}

TEST_F(PresentationSubmissionIntegration, SuboptimalPresentReturnsTheCommittedCompletionPoint)
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
    const auto frame = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_TRUE(frame->completion.isValid());
    EXPECT_TRUE(presentation->get()->drain());
}

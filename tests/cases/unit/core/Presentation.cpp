
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
#include <memory>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <string>

#include "PresentationTestSupport.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "panda/core/SubmissionQueueMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;
using testing::Sequence;
using testing::StrEq;

class PresentationTest : public panda::test::PresentationTestSupport
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(submissions, construct(_, 0)).Times(testing::AnyNumber());
        EXPECT_CALL(submissions, prepareSubmit())
            .Times(testing::AnyNumber())
            .WillRepeatedly(Return(panda::Result<void> {}));
        EXPECT_CALL(submissions, waitValue(0, _))
            .Times(testing::AnyNumber())
            .WillRepeatedly(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Completed}));
    }

    void expectFrameResources(std::size_t count)
    {
        EXPECT_CALL(submissions, initialize(_))
            .WillOnce(testing::Action<panda::Result<void>(vk::PhysicalDevice)> {[](vk::PhysicalDevice) {
                return panda::Result<void> {};
            }});
        expectNativeFrameResources(count - 1);
    }

    void expectResourceDestruction(ResourceDestructionCounts counts)
    {
        --counts.semaphores;
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
            .Times(1)
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
        EXPECT_CALL(submissions, submit(vk::CommandBuffer {}, _, vk::Semaphore {}))
            .InSequence(flow)
            .WillOnce(
                [result](vk::CommandBuffer, vk::Semaphore acquire, vk::Semaphore) -> panda::Result<std::uint64_t> {
                    EXPECT_NE(acquire, vk::Semaphore {});
                    if (result != vk::Result::eSuccess)
                    {
                        return std::unexpected {panda::makeVulkanError(result)};
                    }
                    return 1;
                });
    }

    void expectFrameSubmit(Sequence& flow, vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(submissions, submit(_, _, _))
            .InSequence(flow)
            .WillOnce([result](vk::CommandBuffer command,
                               vk::Semaphore acquire,
                               vk::Semaphore finished) -> panda::Result<std::uint64_t> {
                EXPECT_NE(command, vk::CommandBuffer {});
                EXPECT_NE(acquire, vk::Semaphore {});
                EXPECT_NE(finished, vk::Semaphore {});
                if (result != vk::Result::eSuccess)
                {
                    return std::unexpected {panda::makeVulkanError(result)};
                }
                return 1;
            });
    }

    void expectCompletionPoint(Sequence& flow)
    {
        EXPECT_CALL(submissions, getPoint(1)).InSequence(flow).WillOnce(Return(panda::CompletionPoint {}));
    }

    void expectPendingCounter(Sequence& flow)
    {
        EXPECT_CALL(submissions, prepareSubmit())
            .InSequence(flow)
            .WillOnce(Return(panda::Result<void> {
                std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Timeline limit reached")}}));
    }

    void expectTimelineWait(Sequence& flow, vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(submissions, waitValue(1, 10'000'000'000))
            .InSequence(flow)
            .WillOnce([result](std::uint64_t, std::uint64_t) -> panda::Result<panda::WaitStatus> {
                if (result == vk::Result::eTimeout)
                {
                    return panda::WaitStatus::Timeout;
                }
                if (result != vk::Result::eSuccess)
                {
                    return std::unexpected {panda::makeVulkanError(result)};
                }
                return panda::WaitStatus::Completed;
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
                                                  .swapchainMaintenance = maintenance},
            .submissions = _submissions.emplace(getDevice(), 0)
        });
    }

    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissions;

private:
    std::optional<panda::detail::SubmissionQueue> _submissions;
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

TEST_F(PresentationTest, PreservesSubmissionInitializationFailure)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    EXPECT_CALL(submissions, initialize(_))
        .WillOnce(Return(
            panda::Result<void> {std::unexpected {panda::makeVulkanError(vk::Result::eErrorInitializationFailed)}}));
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, _, _, _)).Times(0);
    const auto presentation = createPresentation();
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(presentation.error().native.has_value());
    const auto native = presentation.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "Vulkan");
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationTest, FailedCommandPoolCreationKeepsBorrowedSubmissionService)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    EXPECT_CALL(submissions, initialize(_)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* output) {
            *output = fakeVulkanHandle<VkCommandPool>(3);
            return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
        });
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, _)).Times(0);
    EXPECT_CALL(vulkan, vkDestroySemaphore(_, _, nullptr)).Times(0);

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
    expectTimelineWait(flow);
    expectFailedRecreation(flow, 700);
    expectTimelineWait(flow);
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
    expectCompletionPoint(flow);
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
    expectCompletionPoint(flow);
    expectTimelineWait(flow);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto frame = presentation->get()->presentClearFrame();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_TRUE(presentation->get()->drain());
}

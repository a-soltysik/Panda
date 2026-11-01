
// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/Presentation.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/RenderView.hpp>
#include <string>
#include <thread>

#include "PresentationTestSupport.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "performance/PerformanceManagerImpl.hpp"

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
        _performance.reset();
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

    void expectPerformancePools()
    {
        auto* const first = fakeVulkanHandle<VkQueryPool>(50);
        auto* const second = fakeVulkanHandle<VkQueryPool>(51);
        EXPECT_CALL(vulkan, vkCreateQueryPool(_, NotNull(), nullptr, NotNull()))
            .Times(2)
            .WillOnce([first](VkDevice,
                              const VkQueryPoolCreateInfo* info,
                              const VkAllocationCallbacks*,
                              VkQueryPool* output) {
                EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                EXPECT_EQ(info->queryCount, 2U);
                *output = first;
                return static_cast<VkResult>(vk::Result::eSuccess);
            })
            .WillOnce([second](VkDevice,
                               const VkQueryPoolCreateInfo* info,
                               const VkAllocationCallbacks*,
                               VkQueryPool* output) {
                EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                EXPECT_EQ(info->queryCount, 2U);
                *output = second;
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyQueryPool(_, first, nullptr));
        EXPECT_CALL(vulkan, vkDestroyQueryPool(_, second, nullptr));
    }

    void expectSampledCommandRecording(Sequence& flow)
    {
        auto* const pool = fakeVulkanHandle<VkQueryPool>(50);
        EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkCmdPipelineBarrier2)> {
                [](VkCommandBuffer, const VkDependencyInfo* dependency) {
                    panda::test::expectTransitionToColorAttachment(dependency);
                }});
        EXPECT_CALL(vulkan, vkCmdResetQueryPool(_, pool, 0, 2)).InSequence(flow);
        EXPECT_CALL(
            vulkan,
            vkCmdWriteTimestamp2(_,
                                 static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                                 pool,
                                 0))
            .InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull())).InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdEndRendering(_)).InSequence(flow);
        EXPECT_CALL(
            vulkan,
            vkCmdWriteTimestamp2(_,
                                 static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                                 pool,
                                 1))
            .InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkCmdPipelineBarrier2)> {
                [](VkCommandBuffer, const VkDependencyInfo* dependency) {
                    panda::test::expectTransitionToPresent(dependency);
                }});
        EXPECT_CALL(vulkan, vkEndCommandBuffer(_))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    }

    void expectCompletedSampleReadback()
    {
        EXPECT_CALL(submissions, poll(_)).WillOnce(Return(panda::Result<bool> {true}));
        auto* const pool = fakeVulkanHandle<VkQueryPool>(50);
        EXPECT_CALL(vulkan,
                    vkGetQueryPoolResults(_,
                                          pool,
                                          0,
                                          2,
                                          sizeof(std::array<std::uint64_t, 4>),
                                          NotNull(),
                                          sizeof(std::uint64_t) * 2,
                                          static_cast<VkQueryResultFlags>(vk::QueryResultFlagBits::e64 |
                                                                          vk::QueryResultFlagBits::eWithAvailability)))
            .WillOnce([](VkDevice,
                         VkQueryPool,
                         std::uint32_t,
                         std::uint32_t,
                         std::size_t,
                         void* data,
                         VkDeviceSize,
                         VkQueryResultFlags) {
                auto* const values = static_cast<std::array<std::uint64_t, 4>*>(data);
                *values = {10, 1, 25, 1};
                return static_cast<VkResult>(vk::Result::eSuccess);
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
                                panda::ContextDeviceInfo::SwapchainMaintenance::Khr,
                            bool enableFramePerformanceSampling = false)
        -> std::expected<std::unique_ptr<panda::core::vulkan::Presentation>, panda::Error>
    {
        auto& submissionQueue = _submissions.emplace(getDevice(), 0);
        if (enableFramePerformanceSampling)
        {
            _performance = std::make_unique<panda::detail::PerformanceManagerImpl>(
                getDevice(),
                &submissionQueue,
                panda::detail::TimestampProperties {.validBits = 64, .periodNanoseconds = 2.5F},
                true);
            if (const auto initialized = _performance->initialize(); !initialized)
            {
                return std::unexpected {initialized.error()};
            }
        }
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
            .submissions = submissionQueue,
            .performance = _performance.get()
        });
    }

    auto performance() -> panda::detail::PerformanceManagerImpl&
    {
        return *_performance;
    }

    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissions;

private:
    std::optional<panda::detail::SubmissionQueue> _submissions;
    std::unique_ptr<panda::detail::PerformanceManagerImpl> _performance;
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
    const auto result = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(result.error().native.has_value());
    const auto native = result.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationTest, RejectsMeshRenderingWithoutContextMemoryPool)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
        .InSequence(flow)
        .WillOnce(
            testing::Action<decltype(vkCmdPipelineBarrier2)> {[](VkCommandBuffer, const VkDependencyInfo* dependency) {
                panda::test::expectTransitionToColorAttachment(dependency);
            }});
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    auto view = panda::RenderView {};
    view.meshes.push_back({});
    const auto frame = presentation->get()->render(view);
    ASSERT_FALSE(frame);
    EXPECT_EQ(frame.error().code, panda::ErrorCode::BackendFailure);
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
        .WillOnce([](VkQueue, const VkPresentInfoKHR*) {
            std::this_thread::sleep_for(std::chrono::milliseconds {2});
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    expectCompletionPoint(flow);
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
    const auto failed = presentation->get()->render(panda::RenderView {});
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
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
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
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
    EXPECT_FALSE(presentation->get()->drain());
}

TEST_F(PresentationTest, RetirementTimeoutDrainReleasesTheAcquiredImage)
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

TEST_F(PresentationTest, FailedImageReleaseKeepsDrainUnsuccessful)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandPoolResetFailure(flow);
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow, vk::Result::eErrorUnknown);
    expectTimelineWait(flow);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    ASSERT_TRUE(failed.error().native.has_value());
    const auto native = failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorUnknown));
    EXPECT_FALSE(presentation->get()->drain());
}

TEST_F(PresentationTest, PresentRecoverySurvivesProducerAndReplacementFailures)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 5, .swapchains = 1});
    expectExtentReads(2);
    expectInitialGeneration(700);
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));

    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow);
    expectPresentFailure(flow);
    expectTimelineWait(flow, vk::Result::eTimeout);
    expectTimelineWait(flow);
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory)));
    expectTimelineWait(flow);
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
        .InSequence(flow)
        .WillOnce([](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* semaphore) {
            *semaphore = fakeVulkanHandle<VkSemaphore>(1000);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    expectImageRelease(flow);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    const auto failed = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::Timeout);
    const auto replacementFailure = presentation->get()->drain();
    ASSERT_FALSE(replacementFailure);
    EXPECT_EQ(replacementFailure.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationTest, OutOfDateAcquireReturnsNoCompletionPoint)
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

TEST_F(PresentationTest, TimeoutAndNotReadyAcquiresSkipWithoutUsingTheImageIndex)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
    expectExtentReads(3);
    expectInitialGeneration(700);
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eTimeout);
    expectAcquiredImage(flow, vk::Result::eNotReady);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);

    const auto timedOut = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(timedOut);
    EXPECT_EQ(timedOut->status, panda::FrameResult::Status::Skipped);
    EXPECT_FALSE(timedOut->completion.isValid());
    const auto notReady = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(notReady);
    EXPECT_EQ(notReady->status, panda::FrameResult::Status::Skipped);
    EXPECT_FALSE(notReady->completion.isValid());
}

TEST_F(PresentationTest, DeviceLostAcquireReturnsTheNativeErrorWithoutUsingTheImageIndex)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eErrorDeviceLost);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);

    const auto failed = presentation->get()->render(panda::RenderView {});

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
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
    const auto frame = presentation->get()->render(panda::RenderView {});
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::RecreatePending);
    EXPECT_TRUE(presentation->get()->drain());
}

TEST_F(PresentationTest, RecordsGpuTimestampsAroundFrameRenderingAndPublishesSubmittedSample)
{
    expectWindowFrame();
    expectPerformancePools();
    EXPECT_CALL(swapchains, encodedClear(_)).WillOnce(Return(vk::ClearColorValue {}));

    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectSampledCommandRecording(flow);
    expectFrameSubmit(flow);
    EXPECT_CALL(submissions, getPoint(1)).InSequence(flow).WillOnce(Return(panda::CompletionPoint {}));
    EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
        .InSequence(flow)
        .WillOnce([](VkQueue, const VkPresentInfoKHR*) {
            std::this_thread::sleep_for(std::chrono::milliseconds {2});
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    expectCompletionPoint(flow);

    auto presentation = createPresentation(panda::ContextDeviceInfo::SwapchainMaintenance::Khr, true);
    ASSERT_TRUE(presentation);
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    const auto frame = presentation->get()->render(panda::RenderView {}, submittedPerformanceSlot);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
    EXPECT_EQ(submittedPerformanceSlot, 0U);

    expectCompletedSampleReadback();
    const auto sample = performance().getLatestSample();
    ASSERT_TRUE(sample);
    ASSERT_TRUE(sample->has_value());
    const auto sampleValue = sample->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(sampleValue.frameIndex, 0U);
    EXPECT_EQ(sampleValue.gpuExecutionDuration, std::chrono::nanoseconds {38});
}

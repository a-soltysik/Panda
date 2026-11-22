
// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/vulkan/Error.hpp>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "DeviceSelection.hpp"
#include "FakeHandle.hpp"
#include "FrameResultMapping.hpp"
#include "PresentationTestSupport.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/FrameExecution.hpp"
#include "presentation/SwapchainGeneration.hpp"
#include "presentation/WindowPresentation.hpp"

namespace
{
using panda::test::fakeHandle;
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

    void expectWindowFrame(std::size_t extentReads = 2)
    {
        expectQueueLookup();
        expectReleaseEntryPoint();
        expectFrameResources(3);
        expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
        expectExtentReads(extentReads);
        expectInitialGeneration(700);
    }

    void expectPresentedFrame(Sequence& flow, vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined)
    {
        expectAcquiredImage(flow, vk::Result::eSuccess);
        expectCommandRecording(flow, oldLayout);
        expectFrameSubmit(flow);
        EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        expectCompletionPoint(flow);
    }

    void expectSupportedModes(bool mailbox)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfacePresentModesKHR(_, _, NotNull(), _))
            .WillOnce(testing::DoAll(testing::SetArgPointee<2>(mailbox ? 3U : 2U), Return(VK_SUCCESS)))
            .WillOnce([mailbox](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t*, VkPresentModeKHR* modes) {
                const auto available = std::array {VK_PRESENT_MODE_FIFO_KHR,
                                                   VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                                                   VK_PRESENT_MODE_MAILBOX_KHR};
                std::ranges::copy(std::span {available}.first(mailbox ? 3U : 2U), modes);
                return VK_SUCCESS;
            });
    }

    void expectModeReplacement(Sequence& flow,
                               std::uintptr_t oldHandle,
                               std::uintptr_t newHandle,
                               panda::EngineOptions::PresentationMode mode)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .InSequence(flow)
            .WillOnce([=](const panda::core::vulkan::SwapchainGenerationRequest& request) {
                EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {fakeHandle<VkSwapchainKHR>(oldHandle)});
                EXPECT_EQ(request.presentationMode, mode);
                return panda::test::makeGeneration(request.device, newHandle);
            });
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
        const auto queryPools = std::array {fakeHandle<VkQueryPool>(50),
                                            fakeHandle<VkQueryPool>(51),
                                            fakeHandle<VkQueryPool>(52),
                                            fakeHandle<VkQueryPool>(53)};
        const auto expectPoolCreation = [this](VkQueryPool queryPool) {
            EXPECT_CALL(vulkan, vkCreateQueryPool(_, NotNull(), nullptr, NotNull()))
                .WillOnce([queryPool](VkDevice,
                                      const VkQueryPoolCreateInfo* info,
                                      const VkAllocationCallbacks*,
                                      VkQueryPool* output) {
                    EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                    EXPECT_EQ(info->queryCount, 2U);
                    *output = queryPool;
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        };
        {
            const auto sequence = testing::InSequence {};
            for (auto* const queryPool : queryPools)
            {
                expectPoolCreation(queryPool);
            }
        }
        for (auto* const queryPool : queryPools)
        {
            EXPECT_CALL(vulkan, vkDestroyQueryPool(_, queryPool, nullptr));
        }
    }

    void expectSampledCommandRecording(Sequence& flow)
    {
        auto* const pool = fakeHandle<VkQueryPool>(50);
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
        auto* const pool = fakeHandle<VkQueryPool>(50);
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

    auto createPresentation(
        panda::core::vulkan::SwapchainMaintenance maintenance = panda::core::vulkan::SwapchainMaintenance::Khr,
        bool enableFramePerformanceSampling = false)
        -> std::expected<std::unique_ptr<panda::core::vulkan::WindowPresentation>, panda::Error>
    {
        auto& submissionQueue = _submissions.emplace(getDevice(), 0);
        EXPECT_CALL(submissions, initialize(_)).WillOnce(Return(panda::Result<void> {}));
        if (const auto initialized = submissionQueue.initialize(vk::PhysicalDevice {}); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
        if (enableFramePerformanceSampling)
        {
            _performance = std::make_unique<panda::detail::PerformanceManagerImpl>(
                getDevice(),
                submissionQueue,
                panda::detail::TimestampProperties {.validBits = 64, .periodNanoseconds = 2.5F});
            if (const auto initialized = _performance->initialize(); !initialized)
            {
                return std::unexpected {initialized.error()};
            }
        }
        return panda::core::vulkan::WindowPresentation::create({.physicalDevice = vk::PhysicalDevice {},
                                                                .device = getDevice(),
                                                                .surface = vk::SurfaceKHR {},
                                                                .window = window,
                                                                .queueFamily = 0,
                                                                .presentQueueFamily = 0,
                                                                .maintenance = maintenance,
                                                                .submissions = submissionQueue,
                                                                .performance = _performance.get(),
                                                                .textureFilteringPreset = nullptr,
                                                                .maxLights = panda::EngineOptions::defaultMaxLights});
    }

    auto createFrameExecution(bool enableFramePerformanceSampling = false)
        -> std::expected<std::unique_ptr<panda::core::vulkan::presentation::FrameExecution>, panda::Error>
    {
        auto& submissionQueue = _submissions.emplace(getDevice(), 0);
        if (enableFramePerformanceSampling)
        {
            _performance = std::make_unique<panda::detail::PerformanceManagerImpl>(
                getDevice(),
                submissionQueue,
                panda::detail::TimestampProperties {.validBits = 64, .periodNanoseconds = 2.5F});
            if (const auto initialized = _performance->initialize(); !initialized)
            {
                return std::unexpected {initialized.error()};
            }
        }
        return panda::core::vulkan::presentation::FrameExecution::create(
            {.physicalDevice = vk::PhysicalDevice {},
             .device = getDevice(),
             .queueFamily = 0,
             .submissions = submissionQueue,
             .performance = _performance.get(),
             .textureFilteringPreset = nullptr,
             .maxLights = panda::EngineOptions::defaultMaxLights});
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

TEST(FrameResultMapping, ConvertsSupportedVulkanSampleCounts)
{
    EXPECT_EQ(panda::core::vulkan::mapSampleCount(vk::SampleCountFlagBits::e1), panda::SampleCount::One);
    EXPECT_EQ(panda::core::vulkan::mapSampleCount(vk::SampleCountFlagBits::e2), panda::SampleCount::Two);
    EXPECT_EQ(panda::core::vulkan::mapSampleCount(vk::SampleCountFlagBits::e4), panda::SampleCount::Four);
}

TEST(FrameResultMapping, RejectsUnsupportedVulkanSampleCounts)
{
    EXPECT_DEATH(static_cast<void>(panda::core::vulkan::mapSampleCount(vk::SampleCountFlagBits::e8)),
                 "Renderer selected an unsupported public sample count");
}

TEST_F(PresentationTest, FrameExecutionRecordsPlainFrameOutputWithoutWindowServices)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    const auto imageView = vk::ImageView {fakeHandle<VkImageView>(900)};
    EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _)).WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull()))
        .WillOnce([imageView](VkCommandBuffer, const VkRenderingInfo* rendering) {
            ASSERT_EQ(rendering->colorAttachmentCount, 1U);
            ASSERT_NE(rendering->pColorAttachments, nullptr);
            const auto& attachment = std::span {rendering->pColorAttachments, rendering->colorAttachmentCount}.front();
            EXPECT_EQ(attachment.imageView, static_cast<VkImageView>(imageView));
            EXPECT_NEAR(std::span {attachment.clearValue.color.float32}.front(), 0.129F, 0.002F);
            EXPECT_EQ(rendering->renderArea.extent.width, 320U);
            EXPECT_EQ(rendering->renderArea.extent.height, 200U);
        });
    EXPECT_CALL(vulkan, vkCmdEndRendering(_));
    EXPECT_CALL(vulkan, vkEndCommandBuffer(_)).WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));

    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    auto& frameExecution = **execution;
    const auto view = panda::RenderView {};
    ASSERT_TRUE(frameExecution.prepareSlot(0, view));
    ASSERT_TRUE(frameExecution.beginCommands(0));
    ASSERT_TRUE(frameExecution.recordContent(0,
                                             {
                                                 .imageView = imageView,
                                                 .extent = {.width = 320, .height = 200},
                                                 .colorFormat = vk::Format::eR8G8B8A8Unorm
    },
                                             view));
    EXPECT_TRUE(frameExecution.endCommands(0));
}

TEST_F(PresentationTest, FrameExecutionClearsAdditionalOutputForPlainFrames)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    const auto first = vk::ImageView {fakeHandle<VkImageView>(901)};
    const auto second = vk::ImageView {fakeHandle<VkImageView>(902)};
    auto recordedViews = std::vector<VkImageView> {};
    EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _)).WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull()))
        .Times(2)
        .WillRepeatedly([&recordedViews](VkCommandBuffer, const VkRenderingInfo* rendering) {
            ASSERT_EQ(rendering->colorAttachmentCount, 1U);
            recordedViews.push_back(rendering->pColorAttachments->imageView);
        });
    EXPECT_CALL(vulkan, vkCmdEndRendering(_)).Times(2);
    EXPECT_CALL(vulkan, vkEndCommandBuffer(_)).WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));

    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    const auto view = panda::RenderView {};
    const auto additionalOutput = panda::core::vulkan::presentation::FrameOutput {
        .imageView = second,
        .extent = {.width = 320, .height = 200},
        .colorFormat = vk::Format::eB8G8R8A8Srgb
    };
    ASSERT_TRUE((*execution)->prepareSlot(0, view));
    ASSERT_TRUE((*execution)->beginCommands(0));
    ASSERT_TRUE((*execution)
                    ->recordContent(0,
                                    {
                                        .imageView = first,
                                        .extent = {.width = 320, .height = 200},
                                        .colorFormat = vk::Format::eR8G8B8A8Unorm
    },
                                    view,
                                    &additionalOutput));
    ASSERT_TRUE((*execution)->endCommands(0));
    EXPECT_THAT(recordedViews, testing::ElementsAre(first, second));
}

TEST_F(PresentationTest, FrameExecutionReportsUnitAnisotropyWithoutAFilteringPreset)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    auto view = panda::RenderView {};
    view.meshes.emplace_back();

    EXPECT_FLOAT_EQ((*execution)->getEffectiveAnisotropy(view), 1.0F);
}

TEST_F(PresentationTest, FrameExecutionPropagatesCommandBufferAllocationFailure)
{
    EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* output) {
            *output = fakeHandle<VkCommandPool>(901);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, fakeHandle<VkCommandPool>(901), _));

    const auto execution = createFrameExecution();

    ASSERT_FALSE(execution);
    ASSERT_TRUE(execution.error().native.has_value());
    const auto native = execution.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory));
}

TEST_F(PresentationTest, FrameExecutionPropagatesCommandBeginAndEndFailures)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _)).WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    EXPECT_CALL(vulkan, vkEndCommandBuffer(_)).WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));

    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);

    const auto begun = (*execution)->beginCommands(0);
    ASSERT_FALSE(begun);
    ASSERT_TRUE(begun.error().native.has_value());
    const auto beginError = begun.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(beginError.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));

    const auto ended = (*execution)->endCommands(0);
    ASSERT_FALSE(ended);
    ASSERT_TRUE(ended.error().native.has_value());
    const auto endError = ended.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(endError.code, static_cast<VkResult>(vk::Result::eErrorDeviceLost));
}

TEST_F(PresentationTest, FrameExecutionPropagatesSlotWaitTimeout)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    EXPECT_CALL(submissions, waitValue(1, 10'000'000'000))
        .Times(2)
        .WillRepeatedly(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Timeout}));

    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    (*execution)->setLastCompletion(0, 1);

    const auto prepared = (*execution)->prepareSlot(0, panda::RenderView {});
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::Timeout);

    const auto drained = (*execution)->drain();
    ASSERT_FALSE(drained);
    EXPECT_EQ(drained.error().code, panda::ErrorCode::Timeout);
}

TEST_F(PresentationTest, FrameExecutionPropagatesTimestampCollectionFailure)
{
    expectNativeFrameResources(0);
    expectNativeResourceDestruction({});
    expectPerformancePools();
    EXPECT_CALL(submissions, getPoint(1)).WillOnce(Return(panda::CompletionPoint {}));
    EXPECT_CALL(submissions, poll(_))
        .WillOnce(Return(panda::Result<bool> {std::unexpected {panda::makeVulkanError(vk::Result::eErrorDeviceLost)}}));

    auto execution = createFrameExecution(true);
    ASSERT_TRUE(execution);
    (*execution)->markSubmitted(0, 1, false);

    const auto recorded = (*execution)->recordContent(0, {}, panda::RenderView {});
    ASSERT_FALSE(recorded);
    EXPECT_EQ(recorded.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(PresentationTest, ReportsSlotWaitTimeoutBeforeAcquiringAnImage)
{
    expectWindowFrame(4);
    auto flow = Sequence {};
    expectPresentedFrame(flow);
    expectAcquiredImage(flow, vk::Result::eSuccess);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkResetFences(_, 1, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    expectCommandRecording(flow, vk::ImageLayout::ePresentSrcKHR);
    expectFrameSubmit(flow);
    EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    expectCompletionPoint(flow);
    EXPECT_CALL(submissions, waitValue(1, 10'000'000'000))
        .InSequence(flow)
        .WillOnce(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Timeout}));

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    ASSERT_TRUE(presentation->get()->render(panda::RenderView {}));
    ASSERT_TRUE(presentation->get()->render(panda::RenderView {}));

    const auto timedOut = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(timedOut);
    EXPECT_EQ(timedOut.error().code, panda::ErrorCode::Timeout);
}

TEST_F(PresentationTest, RecoversAcquireWhenPresentFenceWaitOrResetFails)
{
    expectWindowFrame(4);
    auto flow = Sequence {};
    expectPresentedFrame(flow);
    expectAcquiredImage(flow, vk::Result::eSuccess);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);
    expectTimelineWait(flow);
    expectAcquiredImage(flow, vk::Result::eSuccess);
    EXPECT_CALL(vulkan, vkWaitForFences(_, 1, NotNull(), _, _))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(vulkan, vkResetFences(_, 1, NotNull()))
        .InSequence(flow)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    expectRetirementSubmit(flow);
    expectTimelineWait(flow);
    expectImageRelease(flow);

    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    ASSERT_TRUE(presentation->get()->render(panda::RenderView {}));

    const auto waitFailure = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(waitFailure);
    ASSERT_TRUE(waitFailure.error().native.has_value());
    const auto waitError = waitFailure.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(waitError.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));

    const auto resetFailure = presentation->get()->render(panda::RenderView {});
    ASSERT_FALSE(resetFailure);
    ASSERT_TRUE(resetFailure.error().native.has_value());
    const auto resetError = resetFailure.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(resetError.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(PresentationTest, PropagatesExecutionDrainTimeout)
{
    expectWindowFrame(1);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    EXPECT_CALL(submissions, waitValue(0, 10'000'000'000))
        .WillOnce(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Timeout}));

    const auto drained = presentation->get()->drain();

    ASSERT_FALSE(drained);
    EXPECT_EQ(drained.error().code, panda::ErrorCode::Timeout);
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
    const auto presentation = createPresentation(panda::core::vulkan::SwapchainMaintenance::Ext);
    ASSERT_FALSE(presentation.has_value());
    EXPECT_EQ(presentation.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(presentation.error().message.find("VK_EXT_swapchain_maintenance1"), std::string::npos);
}

TEST_F(PresentationTest, PropagatesAcquireSemaphoreInitializationFailure)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectNativeCommandResources();
    auto* const semaphore = fakeHandle<VkSemaphore>(1001);
    EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
        .Times(2)
        .WillOnce([semaphore](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* out) {
            *out = semaphore;
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory)));
    EXPECT_CALL(vulkan, vkDestroySemaphore(_, semaphore, _));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, _)).Times(2);

    const auto presentation = createPresentation();

    ASSERT_FALSE(presentation);
    ASSERT_TRUE(presentation.error().native.has_value());
    const auto native = presentation.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory));
}

TEST_F(PresentationTest, FailedCommandPoolCreationKeepsBorrowedSubmissionService)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
        .WillOnce([](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* output) {
            *output = fakeHandle<VkCommandPool>(3);
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

TEST_F(PresentationTest, RejectsMeshRenderingWithoutEngineMemoryPoolBeforeAcquire)
{
    expectWindowFrame();
    EXPECT_CALL(vulkan, vkAcquireNextImageKHR(_, _, _, _, _, _)).Times(0);

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

TEST_F(PresentationTest, AppliesSupportedPresentationChangesAtFrameBoundaries)
{
    using Mode = panda::EngineOptions::PresentationMode;
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 6, .swapchains = 3});
    expectExtentReads(3);
    expectInitialGeneration(700);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    auto& windowPresentation = **presentation;

    expectSupportedModes(true);
    const auto supported = windowPresentation.getSupportedPresentationModes();
    ASSERT_TRUE(supported);
    EXPECT_THAT(*supported, testing::ElementsAre(Mode::Fifo, Mode::Mailbox));

    auto flow = Sequence {};
    expectModeReplacement(flow, 700, 800, Mode::Mailbox);
    expectAcquiredImage(flow, vk::Result::eTimeout);
    expectModeReplacement(flow, 800, 900, Mode::Fifo);
    expectAcquiredImage(flow, vk::Result::eTimeout);

    expectSupportedModes(true);
    ASSERT_TRUE(windowPresentation.setPresentationMode(Mode::Mailbox));
    EXPECT_EQ(windowPresentation.getRequestedPresentationMode(), Mode::Mailbox);
    EXPECT_EQ(windowPresentation.getEffectivePresentationMode(), Mode::Fifo);
    ASSERT_TRUE(windowPresentation.render(panda::RenderView {}));
    EXPECT_EQ(windowPresentation.getEffectivePresentationMode(), Mode::Mailbox);

    expectSupportedModes(true);
    ASSERT_TRUE(windowPresentation.setPresentationMode(Mode::Fifo));
    EXPECT_EQ(windowPresentation.getRequestedPresentationMode(), Mode::Fifo);
    EXPECT_EQ(windowPresentation.getEffectivePresentationMode(), Mode::Mailbox);
    ASSERT_TRUE(windowPresentation.render(panda::RenderView {}));
    EXPECT_EQ(windowPresentation.getEffectivePresentationMode(), Mode::Fifo);
}

TEST_F(PresentationTest, RejectsUnsupportedPresentationModeWithoutReplacingSwapchain)
{
    using Mode = panda::EngineOptions::PresentationMode;
    expectWindowFrame();
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);
    auto& windowPresentation = **presentation;

    expectSupportedModes(false);
    const auto request = windowPresentation.setPresentationMode(Mode::Mailbox);
    ASSERT_FALSE(request);
    EXPECT_EQ(request.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(windowPresentation.getRequestedPresentationMode(), Mode::Fifo);
    EXPECT_EQ(windowPresentation.getEffectivePresentationMode(), Mode::Fifo);

    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eTimeout);
    ASSERT_TRUE(windowPresentation.render(panda::RenderView {}));
}

TEST_F(PresentationTest, TimelineLimitRejectsFrameBeforeAcquiringAnotherImage)
{
    expectQueueLookup();
    expectReleaseEntryPoint();
    expectFrameResources(3);
    expectResourceDestruction(ResourceDestructionCounts {.semaphores = 4, .swapchains = 1});
    expectExtentReads(3);
    expectInitialGeneration(700);
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
            *semaphore = fakeHandle<VkSemaphore>(1000);
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

TEST_F(PresentationTest, SurfaceLostPresentFaultsTheWindowPresentation)
{
    expectWindowFrame();
    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    expectCommandRecording(flow);
    expectFrameSubmit(flow);
    expectPresentFailure(flow, vk::Result::eErrorSurfaceLostKHR);
    expectCompletionPoint(flow);
    auto presentation = createPresentation();
    ASSERT_TRUE(presentation);

    const auto failed = presentation->get()->render(panda::RenderView {});

    ASSERT_FALSE(failed);
    ASSERT_TRUE(failed.error().native.has_value());
    EXPECT_EQ(failed.error().native->code, static_cast<VkResult>(vk::Result::eErrorSurfaceLostKHR));
    EXPECT_FALSE(presentation->get()->render(panda::RenderView {}));
}

TEST_F(PresentationTest, RecordsGpuTimestampsAroundFrameRenderingAndPublishesSubmittedSample)
{
    expectWindowFrame();
    expectPerformancePools();

    auto flow = Sequence {};
    expectAcquiredImage(flow, vk::Result::eSuccess);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) -- gMock owns the one-shot actions registered by this helper.
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

    auto presentation = createPresentation(panda::core::vulkan::SwapchainMaintenance::Khr, true);
    ASSERT_TRUE(presentation);
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    const auto frame = presentation->get()->render(panda::RenderView {}, submittedPerformanceSlot);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Rendered);
    EXPECT_EQ(submittedPerformanceSlot, 0U);

    expectCompletedSampleReadback();
    const auto sample = performance().getLatestSample();
    ASSERT_TRUE(sample);
    ASSERT_TRUE(sample->has_value());
    const auto sampleValue = sample->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(sampleValue.frameIndex, 0U);
    EXPECT_EQ(sampleValue.gpuExecutionDuration, std::chrono::nanoseconds {38});
}

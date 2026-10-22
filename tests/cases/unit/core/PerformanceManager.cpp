// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/PerformanceManager.hpp>
#include <tuple>

#include "PerformanceManagerImpl.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"

namespace
{
using panda::detail::PerformanceManagerImpl;
using panda::detail::SubmissionQueue;
using panda::detail::TimestampProperties;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;

constexpr auto success = VK_SUCCESS;
constexpr auto unavailable = VK_NOT_READY;
constexpr auto exhausted = VK_ERROR_OUT_OF_DEVICE_MEMORY;
constexpr auto resultFlags =
    static_cast<VkQueryResultFlags>(vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);

class PerformanceManagerTest : public testing::Test
{
protected:
    PerformanceManagerTest()
        : instance {loader, fakeVulkanHandle<VkInstance>(1)},
          physicalDevice {instance, fakeVulkanHandle<VkPhysicalDevice>(2)},
          device {physicalDevice, fakeVulkanHandle<VkDevice>(3)}
    {
    }

    void SetUp() override
    {
        EXPECT_CALL(submissionQueue, construct(_, 0)).Times(testing::AnyNumber());
        EXPECT_CALL(submissionQueue, getPoint(_))
            .Times(testing::AnyNumber())
            .WillRepeatedly(testing::Return(panda::CompletionPoint {}));
        EXPECT_CALL(submissionQueue, poll(_))
            .Times(testing::AnyNumber())
            .WillRepeatedly(testing::Return(panda::Result<bool> {true}));
    }

    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectPoolCreation(VkQueryPool handle, testing::Sequence& flow, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkCreateQueryPool(_, NotNull(), nullptr, NotNull()))
            .InSequence(flow)
            .WillOnce([handle, result](VkDevice,
                                       const VkQueryPoolCreateInfo* info,
                                       const VkAllocationCallbacks*,
                                       VkQueryPool* output) {
                EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                EXPECT_EQ(info->queryCount, 2U);
                if (result == success)
                {
                    *output = handle;
                }
                return result;
            });
        if (result == success)
        {
            EXPECT_CALL(vulkan, vkDestroyQueryPool(_, handle, nullptr));
        }
    }

    void expectPools()
    {
        auto flow = testing::Sequence {};
        expectPoolCreation(pool0, flow);
        expectPoolCreation(pool1, flow);
    }

    void expectResults(VkQueryPool handle, const std::array<std::uint64_t, 4>& values, VkResult result = success)
    {
        EXPECT_CALL(vulkan,
                    vkGetQueryPoolResults(_,
                                          handle,
                                          0,
                                          2,
                                          sizeof(std::array<std::uint64_t, 4>),
                                          _,
                                          sizeof(std::uint64_t) * 2,
                                          resultFlags))
            .WillOnce([values, result](VkDevice,
                                       VkQueryPool,
                                       std::uint32_t,
                                       std::uint32_t,
                                       std::size_t,
                                       void* output,
                                       VkDeviceSize,
                                       VkQueryResultFlags) {
                std::memcpy(output, values.data(), sizeof(std::array<std::uint64_t, 4>));
                return result;
            });
    }

    [[nodiscard]] auto makeManager(std::uint32_t validBits = 64, float period = 2.5F, bool enabled = true)
        -> PerformanceManagerImpl
    {
        return PerformanceManagerImpl {
            device,
            &getSubmissionQueue(),
            TimestampProperties {.validBits = validBits, .periodNanoseconds = period},
            enabled
        };
    }

    auto getSubmissionQueue() -> SubmissionQueue&
    {
        if (!_submissions)
        {
            _submissions.emplace(device, 0);
        }
        return *_submissions;
    }

    void markSubmitted(PerformanceManagerImpl& manager, std::size_t slotIndex)
    {
        manager.markSubmitted(slotIndex, getSubmissionQueue().getPoint(_nextSubmissionValue++));
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance;
    vk::raii::PhysicalDevice physicalDevice;
    vk::raii::Device device;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissionQueue;
    vk::CommandBuffer command {fakeVulkanHandle<VkCommandBuffer>(7)};
    VkQueryPool pool0 {fakeVulkanHandle<VkQueryPool>(5)};
    VkQueryPool pool1 {fakeVulkanHandle<VkQueryPool>(6)};

private:
    std::optional<SubmissionQueue> _submissions;
    std::uint64_t _nextSubmissionValue {1};
};
}

TEST_F(PerformanceManagerTest, DisabledOrUnsupportedSamplingCreatesNoPoolsAndReturnsNoSamples)
{
    auto disabled = makeManager(64, 2.5F, false);
    ASSERT_TRUE(disabled.initialize());
    const auto capabilities = disabled.getCapabilities();
    EXPECT_TRUE(capabilities.hasGpuTimestampQueries);
    EXPECT_FALSE(capabilities.isGpuTimestampSamplingEnabled);
    EXPECT_TRUE(disabled.recordStart(0, command));
    disabled.recordEnd(0, command);
    markSubmitted(disabled, 0);
    EXPECT_TRUE(disabled.takeNextSample());
    ASSERT_TRUE(disabled.takeAvailableSamples());
    EXPECT_TRUE(disabled.takeAvailableSamples()->empty());

    auto unsupported = makeManager(0, 2.5F, true);
    ASSERT_TRUE(unsupported.initialize());
    EXPECT_FALSE(unsupported.getCapabilities().hasGpuTimestampQueries);
    EXPECT_FALSE(unsupported.getCapabilities().isGpuTimestampSamplingEnabled);
    EXPECT_TRUE(unsupported.getLatestSample());
    EXPECT_FALSE(unsupported.getLatestSample()->has_value());
}

TEST_F(PerformanceManagerTest, RejectsInvalidTimestampPropertiesBeforeCreatingPools)
{
    const auto invalidProperties = std::array {
        TimestampProperties {.validBits = 65, .periodNanoseconds = 2.5F                                  },
        TimestampProperties {.validBits = 64, .periodNanoseconds = 0.0F                                  },
        TimestampProperties {.validBits = 64, .periodNanoseconds = std::numeric_limits<float>::infinity()}
    };
    for (const auto properties : invalidProperties)
    {
        auto manager = makeManager(properties.validBits, properties.periodNanoseconds);
        const auto initialized = manager.initialize();
        ASSERT_FALSE(initialized);
        EXPECT_EQ(initialized.error().code, panda::ErrorCode::BackendFailure);
    }
}

TEST_F(PerformanceManagerTest, CreatesTwoTimestampPoolsAndRecordsFrameMarkers)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    auto* const commandHandle = static_cast<VkCommandBuffer>(command);

    EXPECT_CALL(vulkan, vkCmdResetQueryPool(commandHandle, pool0, 0, 2));
    EXPECT_CALL(
        vulkan,
        vkCmdWriteTimestamp2(commandHandle,
                             static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                             pool0,
                             0));
    EXPECT_CALL(
        vulkan,
        vkCmdWriteTimestamp2(commandHandle,
                             static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                             pool0,
                             1));
    ASSERT_TRUE(manager.recordStart(0, command));
    manager.recordEnd(0, command);
    markSubmitted(manager, 0);
}

TEST_F(PerformanceManagerTest, ConvertsReadyTicksToNanosecondsAndLatestDoesNotConsume)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    manager.recordCpuFrameDuration(0, std::chrono::nanoseconds {15});
    expectResults(pool0, {20, 1, 30, 1});

    const auto latest = manager.getLatestSample();
    ASSERT_TRUE(latest);
    ASSERT_TRUE(latest->has_value());
    const auto latestSample = latest->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(latestSample.frameIndex, 0U);
    EXPECT_EQ(latestSample.cpuFrameDuration, std::chrono::nanoseconds {15});
    EXPECT_EQ(latestSample.gpuExecutionDuration, std::chrono::nanoseconds {25});
    const auto repeated = manager.getLatestSample();
    ASSERT_TRUE(repeated);
    ASSERT_TRUE(repeated->has_value());
    const auto repeatedSample = repeated->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(repeatedSample.frameIndex, 0U);

    const auto next = manager.takeNextSample();
    ASSERT_TRUE(next);
    ASSERT_TRUE(next->has_value());
    const auto nextSample = next->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(nextSample.cpuFrameDuration, std::chrono::nanoseconds {15});
    EXPECT_EQ(nextSample.gpuExecutionDuration, std::chrono::nanoseconds {25});
    const auto empty = manager.takeNextSample();
    ASSERT_TRUE(empty);
    EXPECT_FALSE(empty->has_value());
}

TEST_F(PerformanceManagerTest, ReusedQuerySlotWaitsForItsNewSubmissionBeforeReadingOldAvailability)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    manager.recordCpuFrameDuration(0, std::chrono::nanoseconds {15});
    EXPECT_CALL(submissionQueue, poll(_))
        .Times(3)
        .WillOnce(testing::Return(panda::Result<bool> {true}))
        .WillOnce(testing::Return(panda::Result<bool> {false}))
        .WillOnce(testing::Return(panda::Result<bool> {true}));
    EXPECT_CALL(vulkan,
                vkGetQueryPoolResults(_,
                                      pool0,
                                      0,
                                      2,
                                      sizeof(std::array<std::uint64_t, 4>),
                                      _,
                                      sizeof(std::uint64_t) * 2,
                                      resultFlags))
        .Times(2)
        .WillOnce([](VkDevice,
                     VkQueryPool,
                     std::uint32_t,
                     std::uint32_t,
                     std::size_t,
                     void* output,
                     VkDeviceSize,
                     VkQueryResultFlags) {
            const auto values = std::array<std::uint64_t, 4> {10, 1, 20, 1};
            std::memcpy(output, values.data(), sizeof(values));
            return success;
        })
        .WillOnce([](VkDevice,
                     VkQueryPool,
                     std::uint32_t,
                     std::uint32_t,
                     std::size_t,
                     void* output,
                     VkDeviceSize,
                     VkQueryResultFlags) {
            const auto values = std::array<std::uint64_t, 4> {30, 1, 50, 1};
            std::memcpy(output, values.data(), sizeof(values));
            return success;
        });

    const auto oldSample = manager.takeNextSample();
    ASSERT_TRUE(oldSample);
    ASSERT_TRUE(oldSample->has_value());
    EXPECT_EQ(oldSample->value_or(panda::FramePerformanceSample {}).frameIndex, 0U);

    EXPECT_CALL(vulkan, vkCmdResetQueryPool(static_cast<VkCommandBuffer>(command), pool0, 0, 2));
    EXPECT_CALL(
        vulkan,
        vkCmdWriteTimestamp2(static_cast<VkCommandBuffer>(command),
                             static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                             pool0,
                             0));
    EXPECT_CALL(
        vulkan,
        vkCmdWriteTimestamp2(static_cast<VkCommandBuffer>(command),
                             static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
                             pool0,
                             1));
    ASSERT_TRUE(manager.recordStart(0, command));
    manager.recordEnd(0, command);
    markSubmitted(manager, 0);
    manager.recordCpuFrameDuration(0, std::chrono::nanoseconds {25});

    const auto pending = manager.takeNextSample();
    ASSERT_TRUE(pending);
    EXPECT_FALSE(pending->has_value());

    const auto newSample = manager.takeAvailableSamples();
    ASSERT_TRUE(newSample);
    ASSERT_EQ(newSample->size(), 1U);
    EXPECT_EQ(newSample->at(0).frameIndex, 1U);
    EXPECT_EQ(newSample->at(0).cpuFrameDuration, std::chrono::nanoseconds {25});
    EXPECT_EQ(newSample->at(0).gpuExecutionDuration, std::chrono::nanoseconds {50});
}

TEST_F(PerformanceManagerTest, KeepsPartiallyAvailableQueriesPendingUntilBothMarkersAreReady)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {20, 1, 30, 0}, unavailable);
    const auto pending = manager.getLatestSample();
    ASSERT_TRUE(pending);
    EXPECT_FALSE(pending->has_value());

    expectResults(pool0, {20, 1, 30, 1});
    const auto ready = manager.getLatestSample();
    ASSERT_TRUE(ready);
    ASSERT_TRUE(ready->has_value());
    const auto readySample = ready->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(readySample.gpuExecutionDuration, std::chrono::nanoseconds {25});
}

TEST_F(PerformanceManagerTest, KeepsSamplePendingWhenTheStartMarkerIsUnavailable)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {20, 0, 30, 1}, unavailable);

    const auto sample = manager.getLatestSample();

    ASSERT_TRUE(sample);
    EXPECT_FALSE(sample->has_value());
}

TEST_F(PerformanceManagerTest, AppliesQueueValidBitsWhenTheTimestampCounterWraps)
{
    expectPools();
    auto manager = makeManager(8);
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {250, 1, 6, 1});

    const auto sample = manager.takeNextSample();
    ASSERT_TRUE(sample);
    ASSERT_TRUE(sample->has_value());
    const auto sampleValue = sample->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(sampleValue.gpuExecutionDuration, std::chrono::nanoseconds {30});
}

TEST_F(PerformanceManagerTest, DrainsSamplesInFrameOrderAfterOutOfOrderAvailability)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    markSubmitted(manager, 1);
    expectResults(pool0, {100, 1, 110, 0}, unavailable);
    expectResults(pool1, {200, 1, 208, 1});

    const auto newest = manager.getLatestSample();
    ASSERT_TRUE(newest);
    ASSERT_TRUE(newest->has_value());
    const auto newestSample = newest->value_or(panda::FramePerformanceSample {});
    EXPECT_EQ(newestSample.frameIndex, 1U);

    expectResults(pool0, {100, 1, 110, 1});
    const auto all = manager.takeAvailableSamples();
    ASSERT_TRUE(all);
    ASSERT_EQ(all->size(), 2U);
    EXPECT_EQ(all->at(0).frameIndex, 0U);
    EXPECT_EQ(all->at(1).frameIndex, 1U);
    const auto empty = manager.takeAvailableSamples();
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());
}

TEST_F(PerformanceManagerTest, ReportsQueryReadbackFailure)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {}, VK_ERROR_DEVICE_LOST);

    const auto sample = manager.getLatestSample();
    ASSERT_FALSE(sample);
    EXPECT_EQ(sample.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(PerformanceManagerTest, RecordStartPropagatesQueryReadbackFailure)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {}, VK_ERROR_DEVICE_LOST);

    const auto recorded = manager.recordStart(0, command);

    ASSERT_FALSE(recorded);
    EXPECT_EQ(recorded.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(PerformanceManagerTest, RecordStartRejectsSubmittedSamplesWhoseQueriesAreNotReady)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {20, 1, 30, 0}, unavailable);

    const auto recorded = manager.recordStart(0, command);

    ASSERT_FALSE(recorded);
    EXPECT_EQ(recorded.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(PerformanceManagerTest, RejectsNanosecondDurationsOutsideTheSupportedRange)
{
    expectPools();
    auto manager = makeManager(64, std::numeric_limits<float>::max());
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {20, 1, 21, 1});

    const auto sample = manager.getLatestSample();

    ASSERT_FALSE(sample);
    EXPECT_EQ(sample.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(PerformanceManagerTest, RejectsNanosecondDurationAtTheRoundedInt64Limit)
{
    expectPools();
    auto manager = makeManager(64, 9223372036854775808.0F);
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {20, 1, 21, 1});

    const auto sample = manager.getLatestSample();

    ASSERT_FALSE(sample);
    EXPECT_EQ(sample.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(PerformanceManagerTest, NextSamplePropagatesQueryReadbackFailure)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {}, VK_ERROR_DEVICE_LOST);

    const auto sample = manager.takeNextSample();

    ASSERT_FALSE(sample);
    EXPECT_EQ(sample.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(PerformanceManagerTest, AvailableSamplesPropagatesQueryReadbackFailure)
{
    expectPools();
    auto manager = makeManager();
    ASSERT_TRUE(manager.initialize());
    markSubmitted(manager, 0);
    expectResults(pool0, {}, VK_ERROR_DEVICE_LOST);

    const auto samples = manager.takeAvailableSamples();

    ASSERT_FALSE(samples);
    EXPECT_EQ(samples.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(PerformanceManagerTest, ReleasesTheFirstPoolWhenCreatingTheSecondPoolFails)
{
    auto flow = testing::Sequence {};
    expectPoolCreation(pool0, flow);
    expectPoolCreation(pool1, flow, exhausted);
    auto manager = makeManager();

    const auto initialized = manager.initialize();

    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::ResourceExhausted);
}

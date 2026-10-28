// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "SubmissionQueue.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"

namespace
{
using panda::detail::SubmissionQueue;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
constexpr auto success = VK_SUCCESS;
constexpr auto exhausted = VK_ERROR_OUT_OF_DEVICE_MEMORY;

class SubmissionQueueTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(_, 0, 0, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(testing::SetArgPointee<3>(queue));
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                auto* properties = static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(output->pNext);
                ASSERT_NE(properties, nullptr);
                EXPECT_EQ(properties->sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES);
                properties->maxTimelineSemaphoreValueDifference = maximumDifference;
            });
    }

    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectTimeline(VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), nullptr, NotNull()))
            .WillOnce([this, result](VkDevice,
                                     const VkSemaphoreCreateInfo* info,
                                     const VkAllocationCallbacks*,
                                     VkSemaphore* output) {
                const auto* type = static_cast<const VkSemaphoreTypeCreateInfo*>(info->pNext);
                EXPECT_NE(type, nullptr);
                if (type != nullptr)
                {
                    EXPECT_EQ(type->semaphoreType, VK_SEMAPHORE_TYPE_TIMELINE);
                    EXPECT_EQ(type->initialValue, 0);
                }
                *output = semaphore;
                return result;
            })
            .RetiresOnSaturation();
        if (result == success)
        {
            EXPECT_CALL(vulkan, vkDestroySemaphore(_, semaphore, nullptr)).RetiresOnSaturation();
        }
    }

    void expectSubmit(std::uint64_t value, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(queue, 1, NotNull(), VkFence {}))
            .WillOnce([this, value, result](VkQueue, std::uint32_t, const VkSubmitInfo2* info, VkFence) {
                EXPECT_EQ(info->commandBufferInfoCount, 0);
                EXPECT_EQ(info->waitSemaphoreInfoCount, 0);
                EXPECT_EQ(info->signalSemaphoreInfoCount, 1);
                EXPECT_EQ(info->pSignalSemaphoreInfos->semaphore, semaphore);
                EXPECT_EQ(info->pSignalSemaphoreInfos->value, value);
                EXPECT_EQ(info->pSignalSemaphoreInfos->stageMask,
                          static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands));
                return result;
            });
    }

    void expectExternalSignal(VkSemaphore externalSemaphore,
                              std::uint64_t externalValue,
                              std::uint64_t value,
                              VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(queue, 1, NotNull(), VkFence {}))
            .WillOnce([this, externalSemaphore, externalValue, value, result](VkQueue,
                                                                              std::uint32_t,
                                                                              const VkSubmitInfo2* info,
                                                                              VkFence) {
                EXPECT_EQ(info->waitSemaphoreInfoCount, 0);
                EXPECT_EQ(info->signalSemaphoreInfoCount, 2);
                if (info->signalSemaphoreInfoCount != 2)
                {
                    return result;
                }
                const auto signals = std::span {info->pSignalSemaphoreInfos, info->signalSemaphoreInfoCount};
                const auto& externalSignal = signals.front();
                const auto& internalSignal = *std::next(signals.begin());
                EXPECT_EQ(externalSignal.semaphore, externalSemaphore);
                EXPECT_EQ(externalSignal.value, externalValue);
                EXPECT_EQ(internalSignal.semaphore, semaphore);
                EXPECT_EQ(internalSignal.value, value);
                return result;
            });
    }

    void expectExternalWait(VkSemaphore externalSemaphore,
                            std::uint64_t externalValue,
                            std::uint64_t value,
                            VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(queue, 1, NotNull(), VkFence {}))
            .WillOnce([this, externalSemaphore, externalValue, value, result](VkQueue,
                                                                              std::uint32_t,
                                                                              const VkSubmitInfo2* info,
                                                                              VkFence) {
                EXPECT_EQ(info->waitSemaphoreInfoCount, 1);
                if (info->waitSemaphoreInfoCount != 1)
                {
                    return result;
                }
                const auto waits = std::span {info->pWaitSemaphoreInfos, info->waitSemaphoreInfoCount};
                EXPECT_EQ(waits.front().semaphore, externalSemaphore);
                EXPECT_EQ(waits.front().value, externalValue);
                EXPECT_EQ(info->signalSemaphoreInfoCount, 1);
                if (info->signalSemaphoreInfoCount != 1)
                {
                    return result;
                }
                const auto signals = std::span {info->pSignalSemaphoreInfos, info->signalSemaphoreInfoCount};
                EXPECT_EQ(signals.front().semaphore, semaphore);
                EXPECT_EQ(signals.front().value, value);
                return result;
            });
    }

    void expectCounter(std::uint64_t value, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkGetSemaphoreCounterValue(_, semaphore, NotNull()))
            .WillOnce([value, result](VkDevice, VkSemaphore, std::uint64_t* output) {
                *output = value;
                return result;
            });
    }

    void expectWait(std::uint64_t value, std::uint64_t timeout, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkWaitSemaphores(_, NotNull(), timeout))
            .WillOnce([this, value, result](VkDevice, const VkSemaphoreWaitInfo* info, std::uint64_t) {
                EXPECT_EQ(info->semaphoreCount, 1);
                EXPECT_EQ(*info->pSemaphores, semaphore);
                EXPECT_EQ(*info->pValues, value);
                return result;
            });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    VkQueue queue {fakeVulkanHandle<VkQueue>(4)};
    VkSemaphore semaphore {fakeVulkanHandle<VkSemaphore>(5)};
    std::uint64_t maximumDifference {std::numeric_limits<std::uint64_t>::max()};
};
}

TEST_F(SubmissionQueueTest, FailedCreationNeverDestroysTheUndefinedSemaphoreOutput)
{
    expectTimeline(exhausted);
    auto submissions = SubmissionQueue {device, 0};
    const auto initialized = submissions.initialize(*physical);
    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(SubmissionQueueTest, RejectsAnUnsupportedOutstandingValueLimitBeforeSemaphoreCreation)
{
    maximumDifference = 0;
    auto submissions = SubmissionQueue {device, 0};
    const auto initialized = submissions.initialize(*physical);
    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(SubmissionQueueTest, CommitIsPublishedOnlyAfterSuccessAndFailedValuesAreReusable)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1, exhausted);
    const auto failed = submissions.submit({}, {}, {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    expectSubmit(1);
    const auto committed = submissions.submit({}, {}, {});
    ASSERT_TRUE(committed);
    EXPECT_EQ(*committed, 1);
    const auto point = submissions.getPoint(*committed);
    EXPECT_TRUE(point.isValid());
    expectCounter(0);
    const auto pending = submissions.poll(point);
    ASSERT_TRUE(pending);
    EXPECT_FALSE(*pending);
    expectCounter(1);
    const auto completed = submissions.poll(point);
    ASSERT_TRUE(completed);
    EXPECT_TRUE(*completed);
}

TEST_F(SubmissionQueueTest, ExternalTimelineWaitsAndSignalsUseTheContextSubmissionClock)
{
    auto* const externalSemaphore = fakeVulkanHandle<VkSemaphore>(9);
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectExternalSignal(externalSemaphore, 5, 1);
    const auto released = submissions.submitExternalSignal({}, externalSemaphore, 5);
    ASSERT_TRUE(released);
    EXPECT_EQ(*released, 1);
    expectExternalWait(externalSemaphore, 6, 2);
    const auto acquired = submissions.submitExternalWait({}, externalSemaphore, 6);
    ASSERT_TRUE(acquired);
    EXPECT_EQ(*acquired, 2);
}

TEST_F(SubmissionQueueTest, FailedExternalTimelineSubmitDoesNotCommitItsQueueValue)
{
    auto* const externalSemaphore = fakeVulkanHandle<VkSemaphore>(9);
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectExternalSignal(externalSemaphore, 5, 1, exhausted);
    EXPECT_FALSE(submissions.submitExternalSignal({}, externalSemaphore, 5));
    expectExternalSignal(externalSemaphore, 5, 1);
    const auto retried = submissions.submitExternalSignal({}, externalSemaphore, 5);
    ASSERT_TRUE(retried);
    EXPECT_EQ(*retried, 1);
    EXPECT_FALSE(submissions.submitExternalWait({}, {}, 1));
    EXPECT_FALSE(submissions.submitExternalSignal({}, externalSemaphore, 0));
}

TEST_F(SubmissionQueueTest, WaitTimeoutPreservesPendingWorkAndSuccessCachesCompletion)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    const auto submitted = submissions.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    const auto point = submissions.getPoint(*submitted);
    expectWait(1, 7, VK_TIMEOUT);
    const auto timedOut = submissions.wait(point, 7);
    ASSERT_TRUE(timedOut);
    EXPECT_EQ(*timedOut, panda::WaitStatus::Timeout);
    expectWait(1, 99);
    const auto completed = submissions.wait(point, 99);
    ASSERT_TRUE(completed);
    EXPECT_EQ(*completed, panda::WaitStatus::Completed);
    const auto cached = submissions.wait(point, 0);
    ASSERT_TRUE(cached);
    EXPECT_EQ(*cached, panda::WaitStatus::Completed);
}

TEST_F(SubmissionQueueTest, EmptyAndForeignTokensAreRejectedWithoutReadingOrWaiting)
{
    expectTimeline();
    expectTimeline();
    auto first = SubmissionQueue {device, 0};
    auto second = SubmissionQueue {device, 0};
    ASSERT_TRUE(first.initialize(*physical));
    ASSERT_TRUE(second.initialize(*physical));
    expectSubmit(1);
    const auto submitted = first.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    const auto point = first.getPoint(*submitted);
    EXPECT_FALSE(second.poll(point));
    EXPECT_FALSE(second.wait(point, 0));
    const auto empty = panda::CompletionPoint {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_FALSE(first.poll(empty));
    EXPECT_FALSE(first.wait(empty, 0));
}

TEST_F(SubmissionQueueTest, OutstandingLimitCanBeRecoveredAfterGpuCompletion)
{
    maximumDifference = 1;
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    ASSERT_TRUE(submissions.submit({}, {}, {}));
    expectCounter(0);
    const auto blocked = submissions.submit({}, {}, {});
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code, panda::ErrorCode::ResourceExhausted);
    expectCounter(1);
    expectSubmit(2);
    const auto resumed = submissions.submit({}, {}, {});
    ASSERT_TRUE(resumed);
    EXPECT_EQ(*resumed, 2);
}

TEST_F(SubmissionQueueTest, DeviceLossPreventsNewSubmissionAndFurtherWaits)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    const auto submitted = submissions.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    const auto point = submissions.getPoint(*submitted);
    expectCounter(0, VK_ERROR_DEVICE_LOST);
    const auto lost = submissions.poll(point);
    ASSERT_FALSE(lost);
    EXPECT_EQ(lost.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_FALSE(submissions.submit({}, {}, {}));
    EXPECT_FALSE(submissions.wait(point, 1));
}

TEST_F(SubmissionQueueTest, CounterBeyondCommittedWorkIsNotAcceptedAsCompletion)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    const auto submitted = submissions.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    expectCounter(2);
    const auto invalid = submissions.poll(submissions.getPoint(*submitted));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::BackendFailure);
    expectWait(1, 3);
    ASSERT_TRUE(submissions.wait(submissions.getPoint(*submitted), 3));
}

TEST_F(SubmissionQueueTest, FailedWaitPreservesNativeErrorAndDoesNotPublishCompletion)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    const auto submitted = submissions.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    const auto point = submissions.getPoint(*submitted);
    expectWait(1, 3, VK_ERROR_OUT_OF_HOST_MEMORY);
    const auto failed = submissions.wait(point, 3);
    ASSERT_FALSE(failed);
    ASSERT_TRUE(failed.error().native);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code,
              VK_ERROR_OUT_OF_HOST_MEMORY);
    expectCounter(0);
    const auto pending = submissions.poll(point);
    ASSERT_TRUE(pending);
    EXPECT_FALSE(*pending);
    expectWait(1, 4, VK_ERROR_DEVICE_LOST);
    EXPECT_FALSE(submissions.wait(point, 4));
    EXPECT_FALSE(submissions.poll(point));
    EXPECT_FALSE(submissions.submit({}, {}, {}));
}

TEST_F(SubmissionQueueTest, FailedLimitRefreshCanBeRetriedWithoutSkippingAValue)
{
    maximumDifference = 1;
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    ASSERT_TRUE(submissions.submit({}, {}, {}));
    expectCounter(0, VK_ERROR_OUT_OF_HOST_MEMORY);
    const auto failed = submissions.submit({}, {}, {});
    ASSERT_FALSE(failed);
    ASSERT_TRUE(failed.error().native);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code,
              VK_ERROR_OUT_OF_HOST_MEMORY);
    expectCounter(1);
    expectSubmit(2);
    const auto resumed = submissions.submit({}, {}, {});
    ASSERT_TRUE(resumed);
    EXPECT_EQ(*resumed, 2);
}

TEST_F(SubmissionQueueTest, RegressingCounterDoesNotEraseKnownCompletion)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1);
    const auto submitted = submissions.submit({}, {}, {});
    ASSERT_TRUE(submitted);
    const auto point = submissions.getPoint(*submitted);
    expectWait(1, 1);
    ASSERT_TRUE(submissions.wait(point, 1));
    expectCounter(0);
    const auto invalid = submissions.poll(point);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::BackendFailure);
    const auto cached = submissions.wait(point, 0);
    ASSERT_TRUE(cached);
    EXPECT_EQ(*cached, panda::WaitStatus::Completed);
}

TEST_F(SubmissionQueueTest, SubmitDeviceLossPreventsRetry)
{
    expectTimeline();
    auto submissions = SubmissionQueue {device, 0};
    ASSERT_TRUE(submissions.initialize(*physical));
    expectSubmit(1, VK_ERROR_DEVICE_LOST);
    const auto failed = submissions.submit({}, {}, {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    const auto retry = submissions.submit({}, {}, {});
    ASSERT_FALSE(retry);
    EXPECT_EQ(retry.error().code, panda::ErrorCode::DeviceLost);
}

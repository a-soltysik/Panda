// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "UploadManager.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <utility>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "UploadBatchImpl.hpp"
#include "UploadOperation.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "resources/MemoryPool.hpp"

namespace
{
using panda::detail::MemoryPool;
using panda::detail::SubmissionQueue;
using panda::detail::UploadManager;
using panda::detail::UploadOperation;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

class UploadManagerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties) {
                properties->memoryTypeCount = 1;
                properties->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                properties->memoryHeapCount = 1;
                properties->memoryHeaps[0] = {.size = 4096, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                properties->properties.limits.maxMemoryAllocationCount = 1;
                properties->properties.limits.nonCoherentAtomSize = 1;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(properties->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        pool = MemoryPool::create(physical, device);
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submission;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<MemoryPool> pool;
};
}

TEST_F(UploadManagerTest, RejectsAnEmptySubmitBeforeConsultingTheQueue)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};

    const auto result = uploads.submit({});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(UploadManagerTest, PropagatesPreparationFailureBeforeCreatingACommand)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto writes = std::array {UploadOperation {panda::detail::BufferUpload {}}};
    const auto preparationFailure = panda::Result<void> {
        std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Mocked preparation failure")}};
    EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(false));
    EXPECT_CALL(submission, prepareSubmit()).WillOnce(Return(preparationFailure));
    EXPECT_CALL(submission, submit(_, _, _)).Times(0);

    const auto result = uploads.submit(writes);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(uploads.getStats().pendingBatches, 0);
}

TEST_F(UploadManagerTest, CollectStopsImmediatelyWhenTheQueueReportsDeviceLoss)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(true));

    const auto result = uploads.collect();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(UploadManagerTest, BatchLifetimeTracksActiveBatches)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    EXPECT_FALSE(uploads.hasActiveBatches());

    auto batch = uploads.createBatch();

    EXPECT_TRUE(uploads.hasActiveBatches());
    batch.reset();
    EXPECT_FALSE(uploads.hasActiveBatches());
}

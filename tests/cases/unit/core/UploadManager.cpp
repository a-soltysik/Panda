// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "upload/UploadManager.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "panda/core/UploadCommandMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchImpl.hpp"
#include "upload/UploadOperation.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::core::vulkan::upload::UploadManager;
using panda::core::vulkan::upload::UploadOperation;
using panda::detail::BufferResource;
using panda::detail::SubmissionQueue;
using panda::detail::TextureResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
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
        EXPECT_CALL(poolMock, destroy(_));
        pool = MemoryPool::create(physical, device);
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto submitOne(UploadManager& uploads, std::span<const UploadOperation> writes)
        -> panda::Result<panda::CompletionPoint>
    {
        auto* const command = fakeVulkanHandle<VkCommandBuffer>(31);
        EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(false));
        EXPECT_CALL(submission, prepareSubmit()).WillOnce(Return(panda::Result<void> {}));
        EXPECT_CALL(commandMock, create(Ref(device), 0, _, _)).WillOnce(Return(panda::Result<void> {}));
        EXPECT_CALL(commandMock, getStagingSize(_)).WillOnce(Return(64));
        EXPECT_CALL(commandMock, getCommand(_)).WillOnce(Return(vk::CommandBuffer {command}));
        EXPECT_CALL(submission, submit(vk::CommandBuffer {command}, vk::Semaphore {}, vk::Semaphore {}))
            .WillOnce(Return(panda::Result<std::uint64_t> {7}));
        EXPECT_CALL(submission, getPoint(7)).WillOnce(Return(panda::CompletionPoint {}));
        return uploads.submit(writes);
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocationMock;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submission;
    panda::test::ScopedMock<panda::test::BufferResourceMock> bufferMock;
    panda::test::ScopedMock<panda::test::TextureResourceMock> textureMock;
    panda::test::ScopedMock<panda::test::UploadCommandMock> commandMock;
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
    const auto writes = std::array {UploadOperation {panda::core::vulkan::upload::BufferUpload {}}};
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

TEST_F(UploadManagerTest, ValidatesBufferOwnershipUsageAndRangeWithoutStaging)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    auto destination = std::shared_ptr<BufferResource> {};
    const auto description = panda::BufferDesc {.size = 32,
                                                .usage = vk::BufferUsageFlagBits::eTransferDst,
                                                .memory = panda::BufferMemory::Device};
    EXPECT_CALL(bufferMock, create(_, Ref(*pool), _)).WillOnce(Return(panda::Result<void> {}));
    auto created = BufferResource::create(device, *pool, description);
    ASSERT_TRUE(created);
    destination = std::move(*created);
    EXPECT_CALL(bufferMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(bufferMock, getUsage(Ref(*destination))).WillOnce(Return(vk::BufferUsageFlagBits::eVertexBuffer));
    auto missingUsage = uploads.validateWrite(destination, 0, 4);
    ASSERT_FALSE(missingUsage);
    EXPECT_EQ(missingUsage.error().code, panda::ErrorCode::InvalidArgument);

    EXPECT_CALL(bufferMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(false));
    auto foreign = uploads.validateWrite(destination, 0, 4);
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code, panda::ErrorCode::InvalidArgument);

    EXPECT_CALL(bufferMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(bufferMock, getUsage(Ref(*destination))).WillOnce(Return(vk::BufferUsageFlagBits::eTransferDst));
    EXPECT_CALL(bufferMock, getSize(Ref(*destination))).Times(2).WillRepeatedly(Return(32));
    EXPECT_TRUE(uploads.validateWrite(destination, 24, 8));

    EXPECT_CALL(bufferMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(bufferMock, getUsage(Ref(*destination))).WillOnce(Return(vk::BufferUsageFlagBits::eTransferDst));
    EXPECT_CALL(bufferMock, getSize(Ref(*destination))).Times(2).WillRepeatedly(Return(32));
    const auto outOfBounds = uploads.validateWrite(destination, 25, 8);
    ASSERT_FALSE(outOfBounds);
    EXPECT_EQ(outOfBounds.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(UploadManagerTest, RejectsWritesToAnExternallyOwnedBuffer)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto description = panda::BufferDesc {.size = 32,
                                                .usage = vk::BufferUsageFlagBits::eTransferDst,
                                                .memory = panda::BufferMemory::Device};
    EXPECT_CALL(bufferMock, create(_, Ref(*pool), _)).WillOnce(Return(panda::Result<void> {}));
    auto created = BufferResource::create(device, *pool, description);
    ASSERT_TRUE(created);
    auto destination = std::move(*created);
    destination->setExternallyOwned(true);
    EXPECT_CALL(bufferMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(bufferMock, getUsage(_)).Times(0);

    const auto result = uploads.validateWrite(destination, 0, 4);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourcesInUse);
}

TEST_F(UploadManagerTest, CreatesUploadStagingWithTransferSourceUsage)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    EXPECT_CALL(bufferMock,
                create(Ref(device),
                       Ref(*pool),
                       testing::AllOf(testing::Field(&panda::BufferDesc::size, 2048),
                                      testing::Field(&panda::BufferDesc::usage, vk::BufferUsageFlagBits::eTransferSrc),
                                      testing::Field(&panda::BufferDesc::memory, panda::BufferMemory::Upload))))
        .WillOnce(Return(panda::Result<void> {}));

    const auto staging = uploads.createStaging(2048);

    ASSERT_TRUE(staging);
}

TEST_F(UploadManagerTest, ValidatesTextureCopyLayoutAndSourceSize)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 8, .height = 8},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    };
    EXPECT_CALL(textureMock, create(_, Ref(*pool), _)).WillOnce(Return(panda::Result<void> {}));
    auto created = TextureResource::create(device, *pool, description);
    ASSERT_TRUE(created);
    auto destination = std::move(*created);
    const auto region = panda::TextureUploadRegion {
        .offset = {.x = 2,     .y = 1     },
        .extent = {.width = 2, .height = 2},
        .rowPitchBytes = 12
    };
    EXPECT_CALL(textureMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(textureMock, getUsage(Ref(*destination)))
        .Times(2)
        .WillRepeatedly(Return(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled));
    EXPECT_CALL(textureMock, getFormat(Ref(*destination))).WillOnce(Return(vk::Format::eR8G8B8A8Unorm));
    EXPECT_CALL(textureMock, getExtent(Ref(*destination))).WillOnce(Return(vk::Extent2D {.width = 8, .height = 8}));

    const auto layout = uploads.validateTextureWrite(destination, region, 20);

    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->rowLength, 3);
    EXPECT_EQ(layout->bufferOffsetAlignment, 4);
}

TEST_F(UploadManagerTest, RejectsTextureSourceThatDoesNotContainEveryPaddedRow)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 8, .height = 8},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    };
    EXPECT_CALL(textureMock, create(_, Ref(*pool), _)).WillOnce(Return(panda::Result<void> {}));
    auto created = TextureResource::create(device, *pool, description);
    ASSERT_TRUE(created);
    auto destination = std::move(*created);
    const auto region = panda::TextureUploadRegion {
        .offset = {.x = 2,     .y = 1     },
        .extent = {.width = 2, .height = 2},
        .rowPitchBytes = 12
    };
    EXPECT_CALL(textureMock, belongsTo(Ref(*destination), Ref(*pool))).WillOnce(Return(true));
    EXPECT_CALL(textureMock, getUsage(Ref(*destination)))
        .Times(2)
        .WillRepeatedly(Return(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled));
    EXPECT_CALL(textureMock, getFormat(Ref(*destination))).WillOnce(Return(vk::Format::eR8G8B8A8Unorm));
    EXPECT_CALL(textureMock, getExtent(Ref(*destination))).WillOnce(Return(vk::Extent2D {.width = 8, .height = 8}));

    const auto result = uploads.validateTextureWrite(destination, region, 19);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(UploadManagerTest, PreservesCommandCreationFailureBeforeQueueSubmission)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto writes = std::array {UploadOperation {panda::core::vulkan::upload::BufferUpload {}}};
    const auto commandFailure = panda::Result<void> {
        std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled command failure")}};
    EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(false));
    EXPECT_CALL(submission, prepareSubmit()).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(commandMock, create(Ref(device), 0, _, _)).WillOnce(Return(commandFailure));
    EXPECT_CALL(submission, submit(_, _, _)).Times(0);

    const auto result = uploads.submit(writes);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(uploads.getStats().pendingBatches, 0);
}

TEST_F(UploadManagerTest, RetainsPacketAndAccountsStagingUntilCompletion)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto writes = std::array {UploadOperation {panda::core::vulkan::upload::BufferUpload {}}};
    const auto committed = submitOne(uploads, writes);

    ASSERT_TRUE(committed);
    auto stats = uploads.getStats();
    EXPECT_EQ(stats.pendingBatches, 1);
    EXPECT_EQ(stats.pendingStagingBytes, 64);
    EXPECT_EQ(stats.peakStagingBytes, 64);

    const auto point = panda::CompletionPoint {};
    EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(false));
    EXPECT_CALL(submission, getPoint(7)).WillOnce(Return(point));
    EXPECT_CALL(submission, poll(_)).WillOnce(Return(panda::Result<bool> {true}));
    EXPECT_CALL(submission, getCompletedValue()).WillOnce(Return(7));
    EXPECT_TRUE(uploads.collect());
    stats = uploads.getStats();
    EXPECT_EQ(stats.pendingBatches, 0);
    EXPECT_EQ(stats.pendingStagingBytes, 0);
    EXPECT_EQ(stats.peakStagingBytes, 64);
}

TEST_F(UploadManagerTest, TimedOutDrainRetainsPacketUntilACompletedRetry)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto writes = std::array {UploadOperation {panda::core::vulkan::upload::BufferUpload {}}};
    ASSERT_TRUE(submitOne(uploads, writes));

    EXPECT_CALL(submission, waitValue(7, 5))
        .WillOnce(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Timeout}));
    const auto timedOut = uploads.drain(5);
    ASSERT_FALSE(timedOut);
    EXPECT_EQ(timedOut.error().code, panda::ErrorCode::Timeout);
    EXPECT_EQ(uploads.getStats().pendingBatches, 1);

    EXPECT_CALL(submission, waitValue(7, 9))
        .WillOnce(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Completed}));
    EXPECT_CALL(submission, getCompletedValue()).WillOnce(Return(7));
    EXPECT_TRUE(uploads.drain(9));
    EXPECT_EQ(uploads.getStats().pendingBatches, 0);
}

TEST_F(UploadManagerTest, RemovesUncommittedPacketWhenQueueSubmissionFails)
{
    EXPECT_CALL(submission, construct(_, 0));
    auto submissions = SubmissionQueue {device, 0};
    auto uploads = UploadManager {device, *pool, submissions, 0};
    const auto writes = std::array {UploadOperation {panda::core::vulkan::upload::BufferUpload {}}};
    auto* const command = fakeVulkanHandle<VkCommandBuffer>(32);
    const auto failure = panda::Result<std::uint64_t> {
        std::unexpected {panda::makeError(panda::ErrorCode::DeviceLost, "Controlled queue failure")}};
    EXPECT_CALL(submission, isDeviceLost()).WillOnce(Return(false));
    EXPECT_CALL(submission, prepareSubmit()).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(commandMock, create(Ref(device), 0, _, _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(commandMock, getStagingSize(_)).WillOnce(Return(64));
    EXPECT_CALL(commandMock, getCommand(_)).WillOnce(Return(vk::CommandBuffer {command}));
    EXPECT_CALL(submission, submit(vk::CommandBuffer {command}, vk::Semaphore {}, vk::Semaphore {}))
        .WillOnce(Return(failure));

    const auto result = uploads.submit(writes);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_EQ(uploads.getStats().pendingBatches, 0);
    EXPECT_EQ(uploads.getStats().pendingStagingBytes, 0);
}

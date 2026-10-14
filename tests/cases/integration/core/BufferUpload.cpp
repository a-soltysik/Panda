// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "UploadBatchImpl.hpp"
#include "UploadManager.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MemoryPool.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);
constexpr auto payload = std::array {
    std::byte {1}, std::byte {2}, std::byte {3}, std::byte {4}, std::byte {5}, std::byte {6}, std::byte {7}};

struct NativeBuffer
{
    std::uint64_t size;
    VkDeviceMemory memory {};
    std::uint64_t offset {0};
};

struct ObservedCopy
{
    VkBuffer destination;
    std::uint64_t offset;
    std::vector<std::byte> bytes;
};

class BufferUploadIntegration : public testing::Test
{
protected:
    void SetUp() override
    {
        configureProperties();
        configureBuffers();
        configureMemory();
        configureCommands();
        configureSubmissions();
        pool = panda::detail::MemoryPool::create(physical, device);
        submissions = std::make_unique<panda::detail::SubmissionQueue>(device, 0);
        ASSERT_TRUE(submissions->initialize(*physical));
        uploads = std::make_unique<panda::detail::UploadManager>(device, *pool, *submissions, 0);
    }

    void TearDown() override
    {
        ASSERT_FALSE(uploads->hasActiveBatches());
        ASSERT_TRUE(uploads->drain(100));
        uploads.reset();
        pool.reset();
        submissions.reset();
        EXPECT_TRUE(buffers.empty());
        EXPECT_TRUE(memory.empty());
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void configureProperties()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties) {
                *properties = {};
                properties->memoryHeapCount = 1;
                properties->memoryHeaps[0] = {.size = 65536, .flags = 0};
                properties->memoryTypeCount = 1;
                properties->memoryTypes[0] = {.propertyFlags = memoryFlags, .heapIndex = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                const auto* header = static_cast<const VkBaseOutStructure*>(properties->pNext);
                if (header->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES)
                {
                    static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(properties->pNext)
                        ->maxTimelineSemaphoreValueDifference = std::numeric_limits<std::uint64_t>::max();
                    return;
                }
                properties->properties.limits.maxMemoryAllocationCount = 16;
                properties->properties.limits.nonCoherentAtomSize = 64;
                static_cast<VkPhysicalDeviceMaintenance3Properties*>(properties->pNext)->maxMemoryAllocationSize = 4096;
            });
    }

    void configureBuffers()
    {
        EXPECT_CALL(vulkan, vkCreateBuffer(_, NotNull(), nullptr, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkBufferCreateInfo* info, const VkAllocationCallbacks*, VkBuffer* output) {
                    ++bufferCreations;
                    *output = fakeVulkanHandle<VkBuffer>(nextId++);
                    if (bufferStatus == success)
                    {
                        buffers.emplace(*output, NativeBuffer {.size = info->size});
                    }
                    return bufferStatus;
                });
        EXPECT_CALL(vulkan, vkGetBufferMemoryRequirements2(_, NotNull(), NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkBufferMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                    output->memoryRequirements = {.size = buffers.at(info->buffer).size,
                                                  .alignment = 64,
                                                  .memoryTypeBits = 1};
                });
        configureBufferBinding();
    }

    void configureBufferBinding()
    {
        EXPECT_CALL(vulkan, vkBindBufferMemory2(_, 1, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, std::uint32_t, const VkBindBufferMemoryInfo* info) {
                buffers.at(info->buffer).memory = info->memory;
                buffers.at(info->buffer).offset = info->memoryOffset;
                return success;
            });
        EXPECT_CALL(vulkan, vkDestroyBuffer(_, _, nullptr))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) {
                EXPECT_EQ(buffers.erase(buffer), 1);
                destruction.push_back('b');
            });
    }

    void configureMemory()
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkMemoryAllocateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkDeviceMemory* output) {
                *output = fakeVulkanHandle<VkDeviceMemory>(nextId++);
                memory.emplace(*output, std::vector<std::byte>(static_cast<std::size_t>(info->allocationSize)));
                return success;
            });
        EXPECT_CALL(vulkan, vkMapMemory(_, _, _, _, _, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   VkDeviceMemory allocation,
                                   VkDeviceSize,
                                   VkDeviceSize,
                                   VkMemoryMapFlags,
                                   void** output) {
                *output = memory.at(allocation).data();
                return success;
            });
        configureCacheOperations();
    }

    void configureCacheOperations()
    {
        EXPECT_CALL(vulkan, vkUnmapMemory(_, _)).Times(testing::AnyNumber());
        EXPECT_CALL(vulkan, vkFreeMemory(_, _, nullptr))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkDeviceMemory allocation, const VkAllocationCallbacks*) {
                EXPECT_EQ(memory.erase(allocation), 1);
            });
        EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, std::uint32_t, const VkMappedMemoryRange* range) {
                flushed.push_back(*range);
                return flushStatus;
            });
    }

    void configureCommands()
    {
        EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), nullptr, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkCommandPoolCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkCommandPool* output) {
                EXPECT_EQ(info->queueFamilyIndex, 0);
                *output = fakeVulkanHandle<VkCommandPool>(nextId++);
                return poolStatus;
            });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
                EXPECT_EQ(info->commandBufferCount, 1);
                *output = fakeVulkanHandle<VkCommandBuffer>(nextId++);
                return allocationStatus;
            });
        configureCommandRecording();
    }

    void configureCommandRecording()
    {
        EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer, const VkCommandBufferBeginInfo*) {
                return beginStatus;
            });
        EXPECT_CALL(vulkan, vkEndCommandBuffer(_)).Times(testing::AnyNumber()).WillRepeatedly([this](VkCommandBuffer) {
            return endStatus;
        });
        EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, nullptr))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkCommandPool, const VkAllocationCallbacks*) {
                destruction.push_back('p');
            });
        configureCopies();
    }

    void configureCopies()
    {
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer, const VkDependencyInfo* info) {
                ASSERT_EQ(info->memoryBarrierCount, 1);
                ASSERT_NE(info->pMemoryBarriers, nullptr);
                barriers.push_back(*info->pMemoryBarriers);
            });
        // Vulkan fixes these adjacent parameters as source and destination handles.
        // NOLINTBEGIN(bugprone-easily-swappable-parameters)
        EXPECT_CALL(vulkan, vkCmdCopyBuffer(_, _, _, 1, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer,
                                   VkBuffer source,
                                   VkBuffer destination,
                                   std::uint32_t,
                                   const VkBufferCopy* region) {
                EXPECT_TRUE(buffers.contains(destination));
                const auto& view = buffers.at(source);
                const auto bytes =
                    std::span<const std::byte> {memory.at(view.memory)}.subspan(view.offset + region->srcOffset,
                                                                                region->size);
                copies.push_back(ObservedCopy {
                    .destination = destination,
                    .offset = region->dstOffset,
                    .bytes = {bytes.begin(), bytes.end()}
                });
            });
        // NOLINTEND(bugprone-easily-swappable-parameters)
    }

    void configureSubmissions()
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(_, 0, 0, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(testing::SetArgPointee<3>(fakeVulkanHandle<VkQueue>(4)));
        EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), nullptr, NotNull()))
            .WillOnce([](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
                *output = fakeVulkanHandle<VkSemaphore>(5);
                return success;
            });
        EXPECT_CALL(vulkan, vkDestroySemaphore(_, fakeVulkanHandle<VkSemaphore>(5), nullptr));
        configureSubmissionResults();
    }

    void configureSubmissionResults()
    {
        EXPECT_CALL(vulkan, vkQueueSubmit2(_, 1, NotNull(), _))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkQueue, std::uint32_t, const VkSubmitInfo2* info, VkFence) {
                EXPECT_EQ(info->commandBufferInfoCount, 1);
                EXPECT_EQ(info->waitSemaphoreInfoCount, 0);
                EXPECT_EQ(info->signalSemaphoreInfoCount, 1);
                signals.push_back(info->pSignalSemaphoreInfos->value);
                return submitStatus;
            });
        EXPECT_CALL(vulkan, vkGetSemaphoreCounterValue(_, _, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkSemaphore, std::uint64_t* output) {
                *output = completed;
                return counterStatus;
            });
        EXPECT_CALL(vulkan, vkWaitSemaphores(_, NotNull(), _))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, const VkSemaphoreWaitInfo* info, std::uint64_t) {
                ++waits;
                if (waitStatus == success)
                {
                    completed = std::max(completed, *info->pValues);
                }
                return waitStatus;
            });
    }

    auto destination(vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferDst)
        -> std::shared_ptr<panda::detail::BufferResource>
    {
        auto created = panda::detail::BufferResource::create(device, *pool, {.size = 512, .usage = usage});
        return panda::expect(created, "Test destination could not be created");
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<panda::detail::MemoryPool> pool;
    std::unique_ptr<panda::detail::SubmissionQueue> submissions;
    std::unique_ptr<panda::detail::UploadManager> uploads;
    std::unordered_map<VkBuffer, NativeBuffer> buffers;
    std::unordered_map<VkDeviceMemory, std::vector<std::byte>> memory;
    std::vector<ObservedCopy> copies;
    std::vector<VkMemoryBarrier2> barriers;
    std::vector<VkMappedMemoryRange> flushed;
    std::vector<std::uint64_t> signals;
    std::vector<char> destruction;
    VkMemoryPropertyFlags memoryFlags {static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal |
                                                                          vk::MemoryPropertyFlagBits::eHostVisible |
                                                                          vk::MemoryPropertyFlagBits::eHostCoherent)};
    VkResult bufferStatus {success};
    VkResult flushStatus {success};
    VkResult poolStatus {success};
    VkResult allocationStatus {success};
    VkResult beginStatus {success};
    VkResult endStatus {success};
    VkResult submitStatus {success};
    VkResult counterStatus {success};
    VkResult waitStatus {success};
    std::uintptr_t nextId {100};
    std::uint64_t completed {0};
    std::size_t bufferCreations {0};
    std::size_t waits {0};
};
}

TEST_F(BufferUploadIntegration, EmptyBatchAndInvalidWritesProduceNoStaging)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    EXPECT_FALSE(batch->submit());
    EXPECT_FALSE(batch->write({}, 0, payload));
    EXPECT_FALSE(batch->write(target, 0, {}));
    EXPECT_FALSE(batch->write(target, 511, payload));
    EXPECT_FALSE(batch->write(target, std::numeric_limits<std::uint64_t>::max(), payload));
    EXPECT_EQ(bufferCreations, 1);
    EXPECT_TRUE(signals.empty());
}

TEST_F(BufferUploadIntegration, WrongUsageAndForeignPoolAreRejected)
{
    auto target = destination(vk::BufferUsageFlagBits::eStorageBuffer);
    auto batch = uploads->createBatch();
    EXPECT_FALSE(batch->write(target, 0, payload));
    auto otherPool = panda::detail::MemoryPool::create(physical, device);
    auto other = panda::detail::BufferResource::create(device,
                                                       *otherPool,
                                                       {.size = 512, .usage = vk::BufferUsageFlagBits::eTransferDst});
    ASSERT_TRUE(other);
    EXPECT_FALSE(batch->write(*other, 0, payload));
    EXPECT_EQ(bufferCreations, 2);
}

TEST_F(BufferUploadIntegration, WriteSnapshotsCallerBytesAndSubmissionRetainsBothBuffers)
{
    auto target = destination();
    const auto weak = std::weak_ptr {target};
    auto bytes = payload;
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 3, bytes));
    bytes.fill(std::byte {0});
    target.reset();
    const auto submitted = batch->submit();
    ASSERT_TRUE(submitted);
    batch.reset();
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().offset, 3);
    EXPECT_EQ(copies.front().bytes, (std::vector<std::byte> {payload.begin(), payload.end()}));
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(uploads->getStats().pendingBatches, 1);
    EXPECT_EQ(waits, 0);
    ASSERT_TRUE(submissions->wait(*submitted, 100));
    uploads->collectCompleted();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 0);
    ASSERT_GE(destruction.size(), 3);
    EXPECT_EQ(destruction.front(), 'p');
}

TEST_F(BufferUploadIntegration, OverlappingWritesAreRecordedInOrderWithDependencies)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 3, payload));
    ASSERT_TRUE(batch->write(target, 5, payload));
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(copies.size(), 2);
    EXPECT_EQ(copies.front().offset, 3);
    EXPECT_EQ(copies.back().offset, 5);
    ASSERT_EQ(barriers.size(), 3);
    auto barrier = barriers.begin();
    EXPECT_EQ(barrier->srcStageMask, static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands));
    ++barrier;
    EXPECT_EQ(barrier->srcAccessMask,
              static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite));
    EXPECT_EQ(barrier->dstStageMask, static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eCopy));
    ++barrier;
    EXPECT_EQ(barrier->srcAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eTransferWrite));
    EXPECT_EQ(barrier->dstStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands |
                                                 vk::PipelineStageFlagBits2::eHost));
}

TEST_F(BufferUploadIntegration, CancelledBatchReleasesStagingWithoutSubmitting)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    EXPECT_EQ(buffers.size(), 2);
    batch.reset();
    EXPECT_EQ(buffers.size(), 1);
    EXPECT_FALSE(uploads->hasActiveBatches());
    EXPECT_TRUE(signals.empty());
}

TEST_F(BufferUploadIntegration, FailedStagingCreationKeepsEarlierWrites)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    bufferStatus = exhausted;
    const auto failed = batch->write(target, 32, payload);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    bufferStatus = success;
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().offset, 0);
}

TEST_F(BufferUploadIntegration, NoncoherentStagingFlushesWholeAtomsAndFailedFlushPublishesNoWrite)
{
    memoryFlags &= ~static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostCoherent);
    uploads.reset();
    pool = panda::detail::MemoryPool::create(physical, device);
    uploads = std::make_unique<panda::detail::UploadManager>(device, *pool, *submissions, 0);
    auto target = destination();
    auto batch = uploads->createBatch();
    flushStatus = exhausted;
    EXPECT_FALSE(batch->write(target, 0, payload));
    EXPECT_EQ(buffers.size(), 1);
    flushStatus = success;
    ASSERT_TRUE(batch->write(target, 3, payload));
    ASSERT_EQ(flushed.size(), 2);
    EXPECT_EQ(flushed.back().offset % 64, 0);
    EXPECT_EQ(flushed.back().size, 64);
    ASSERT_TRUE(batch->submit());
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 64);
}

TEST_F(BufferUploadIntegration, CommandPreparationFailuresLeaveBatchAvailableForRetry)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    for (auto* status : std::array {&poolStatus, &allocationStatus, &beginStatus, &endStatus})
    {
        *status = exhausted;
        const auto failed = batch->submit();
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
        EXPECT_EQ(uploads->getStats().pendingBatches, 0);
        EXPECT_EQ(buffers.size(), 2);
        *status = success;
    }
    EXPECT_TRUE(signals.empty());
    ASSERT_TRUE(batch->submit());
}

TEST_F(BufferUploadIntegration, FailedSubmitReusesCandidateAndLeavesOriginalStagingAlive)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    submitStatus = exhausted;
    EXPECT_FALSE(batch->submit());
    EXPECT_EQ(uploads->getStats().pendingBatches, 0);
    EXPECT_EQ(buffers.size(), 2);
    submitStatus = success;
    ASSERT_TRUE(batch->submit());
    EXPECT_EQ(signals, (std::vector<std::uint64_t> {1, 1}));
}

TEST_F(BufferUploadIntegration, TimeoutAndFailedCounterKeepResourcesUntilCompletionIsConfirmed)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    const auto point = batch->submit();
    ASSERT_TRUE(point);
    waitStatus = static_cast<VkResult>(vk::Result::eTimeout);
    EXPECT_FALSE(uploads->drain(0));
    EXPECT_EQ(uploads->getStats().pendingBatches, 1);
    counterStatus = static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory);
    EXPECT_FALSE(uploads->collect());
    EXPECT_EQ(buffers.size(), 2);
    counterStatus = success;
    completed = 1;
    ASSERT_TRUE(uploads->collect());
    EXPECT_EQ(uploads->getStats().pendingBatches, 0);
    waitStatus = success;
}

TEST_F(BufferUploadIntegration, CollectionReleasesOnlyTheCompletedPrefixAndReportsPeakStaging)
{
    auto target = destination();
    auto first = uploads->createBatch();
    ASSERT_TRUE(first->write(target, 0, payload));
    ASSERT_TRUE(first->submit());
    auto second = uploads->createBatch();
    ASSERT_TRUE(second->write(target, 32, payload));
    ASSERT_TRUE(second->submit());
    EXPECT_EQ(uploads->getStats().pendingBatches, 2);
    EXPECT_EQ(uploads->getStats().peakStagingBytes, 14);
    completed = 1;
    ASSERT_TRUE(uploads->collect());
    EXPECT_EQ(uploads->getStats().pendingBatches, 1);
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 7);
    EXPECT_EQ(buffers.size(), 2);
    completed = 2;
    ASSERT_TRUE(uploads->collect());
    EXPECT_EQ(uploads->getStats().pendingBatches, 0);
    EXPECT_EQ(buffers.size(), 1);
}

TEST_F(BufferUploadIntegration, DeviceLostSubmitDoesNotRetryTheDriver)
{
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    submitStatus = static_cast<VkResult>(vk::Result::eErrorDeviceLost);
    const auto failed = batch->submit();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    const auto creations = bufferCreations;
    EXPECT_FALSE(batch->write(target, 32, payload));
    EXPECT_EQ(bufferCreations, creations);
    EXPECT_FALSE(batch->submit());
    EXPECT_EQ(signals.size(), 1);
    EXPECT_EQ(uploads->getStats().pendingBatches, 0);
}

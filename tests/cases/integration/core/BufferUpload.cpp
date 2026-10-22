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
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_format_traits.hpp>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchImpl.hpp"
#include "upload/UploadManager.hpp"

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
    std::uint64_t sourceOffset;
    std::uint64_t offset;
    std::vector<std::byte> bytes;
};

struct NativeImage
{
    VkExtent3D extent;
    vk::Format format;
    VkImageUsageFlags usage;
    VkDeviceMemory memory {};
    std::uint64_t offset {0};
};

struct ObservedImageCopy
{
    VkImage destination;
    VkImageLayout layout;
    VkBufferImageCopy region;
    std::vector<std::byte> bytes;
};

auto getImageCopyByteSize(const NativeImage& image, const VkBufferImageCopy& region) -> std::size_t
{
    const auto format = image.format;
    const auto [blockWidth, blockHeight, blockDepth] = vk::blockExtent(format);
    const auto blockSize = static_cast<std::uint64_t>(vk::blockSize(format));
    EXPECT_EQ(blockDepth, 1U);
    const auto blockColumns = (static_cast<std::uint64_t>(region.imageExtent.width) + blockWidth - 1) / blockWidth;
    const auto blockRows = (static_cast<std::uint64_t>(region.imageExtent.height) + blockHeight - 1) / blockHeight;
    const auto rowBytes = blockColumns * blockSize;
    const auto rowPitch = region.bufferRowLength == 0
                              ? rowBytes
                              : (static_cast<std::uint64_t>(region.bufferRowLength) / blockWidth) * blockSize;
    return ((blockRows - 1) * rowPitch) + rowBytes;
}

class BufferUploadIntegration : public testing::Test
{
protected:
    void SetUp() override
    {
        configureProperties();
        configureBuffers();
        configureImages();
        configureImageViews();
        configureMemory();
        configureCommands();
        configureSubmissions();
        pool = panda::core::vulkan::resources::MemoryPool::create(physical, device);
        submissions = std::make_unique<panda::detail::SubmissionQueue>(device, 0);
        ASSERT_TRUE(submissions->initialize(*physical));
        uploads = std::make_unique<panda::core::vulkan::upload::UploadManager>(device, *pool, *submissions, 0);
    }

    void TearDown() override
    {
        ASSERT_FALSE(uploads->hasActiveBatches());
        ASSERT_TRUE(uploads->drain(100));
        uploads.reset();
        pool.reset();
        submissions.reset();
        EXPECT_TRUE(buffers.empty());
        EXPECT_TRUE(images.empty());
        EXPECT_TRUE(imageViews.empty());
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
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                const auto* header = static_cast<const VkBaseOutStructure*>(properties->pNext);
                if (header->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES)
                {
                    static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(properties->pNext)
                        ->maxTimelineSemaphoreValueDifference = std::numeric_limits<std::uint64_t>::max();
                    return;
                }
                properties->properties.limits.maxMemoryAllocationCount = 16;
                properties->properties.limits.nonCoherentAtomSize = 64;
                static_cast<VkPhysicalDeviceMaintenance3Properties*>(properties->pNext)->maxMemoryAllocationSize =
                    maximumAllocationSize;
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
                    const auto result = bufferCreations == failBufferCreationAt ? exhausted : bufferStatus;
                    if (result == success)
                    {
                        buffers.emplace(*output, NativeBuffer {.size = info->size});
                    }
                    return result;
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

    void configureImages()
    {
        configureImageFormatProperties();
        EXPECT_CALL(vulkan, vkCreateImage(_, NotNull(), nullptr, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* output) {
                    *output = fakeVulkanHandle<VkImage>(nextId++);
                    images.emplace(*output,
                                   NativeImage {.extent = info->extent,
                                                .format = static_cast<vk::Format>(info->format),
                                                .usage = info->usage});
                    return success;
                });
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(_, NotNull(), NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                    output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 1};
                    auto* requirements = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                    if (requirements != nullptr)
                    {
                        requirements->requiresDedicatedAllocation = VK_FALSE;
                        requirements->prefersDedicatedAllocation = VK_FALSE;
                    }
                    EXPECT_TRUE(images.contains(info->image));
                });
        EXPECT_CALL(vulkan, vkBindImageMemory2(_, 1, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                images.at(info->image).memory = info->memory;
                images.at(info->image).offset = info->memoryOffset;
                return success;
            });
        EXPECT_CALL(vulkan, vkDestroyImage(_, _, nullptr))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkImage image, const VkAllocationCallbacks*) {
                EXPECT_FALSE(std::ranges::any_of(imageViews, [image](const auto& entry) {
                    return entry.second == image;
                }));
                EXPECT_EQ(images.erase(image), 1);
                destruction.push_back('i');
            });
    }

    void configureImageFormatProperties()
    {
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice,
                               VkFormat,
                               VkImageType,
                               VkImageTiling,
                               VkImageUsageFlags,
                               VkImageCreateFlags,
                               VkImageFormatProperties* output) {
                *output = VkImageFormatProperties {};
                output->maxExtent = {.width = std::numeric_limits<std::uint32_t>::max(),
                                     .height = std::numeric_limits<std::uint32_t>::max(),
                                     .depth = 1};
                return success;
            });
    }

    void configureImageViews()
    {
        EXPECT_CALL(vulkan, vkCreateImageView(_, NotNull(), nullptr, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* output) {
                    EXPECT_TRUE(images.contains(info->image));
                    EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D);
                    auto* const view = fakeVulkanHandle<VkImageView>(nextId++);
                    *output = view;
                    imageViews.emplace(view, info->image);
                    return success;
                });
        EXPECT_CALL(vulkan, vkDestroyImageView(_, _, nullptr))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkDevice, VkImageView view, const VkAllocationCallbacks*) {
                EXPECT_EQ(imageViews.erase(view), 1);
                destruction.push_back('v');
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
                return flushed.size() == failFlushAt ? exhausted : flushStatus;
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
        configureBarriers();
        configureBufferCopies();
        configureTextureCopies();
    }

    void configureBarriers()
    {
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer, const VkDependencyInfo* info) {
                if (info->memoryBarrierCount != 0)
                {
                    ASSERT_EQ(info->memoryBarrierCount, 1);
                    ASSERT_NE(info->pMemoryBarriers, nullptr);
                    barriers.push_back(*info->pMemoryBarriers);
                }
                const auto imageDependencies =
                    std::span<const VkImageMemoryBarrier2> {info->pImageMemoryBarriers, info->imageMemoryBarrierCount};
                std::ranges::copy(imageDependencies, std::back_inserter(imageBarriers));
            });
    }

    void configureBufferCopies()
    {
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
                    .sourceOffset = region->srcOffset,
                    .offset = region->dstOffset,
                    .bytes = {bytes.begin(), bytes.end()}
                });
                copyOrder.push_back('b');
            });
        // NOLINTEND(bugprone-easily-swappable-parameters)
    }

    void configureTextureCopies()
    {
        EXPECT_CALL(vulkan, vkCmdCopyBufferToImage(_, _, _, _, 1, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer,
                                   VkBuffer source,
                                   VkImage destination,
                                   VkImageLayout layout,
                                   std::uint32_t,
                                   const VkBufferImageCopy* region) {
                EXPECT_TRUE(images.contains(destination));
                const auto& view = buffers.at(source);
                const auto byteSize = getImageCopyByteSize(images.at(destination), *region);
                const auto bytes =
                    std::span<const std::byte> {memory.at(view.memory)}.subspan(view.offset + region->bufferOffset,
                                                                                byteSize);
                imageCopies.push_back(ObservedImageCopy {
                    .destination = destination,
                    .layout = layout,
                    .region = *region,
                    .bytes = {bytes.begin(), bytes.end()}
                });
                copyOrder.push_back('i');
            });
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

    void useNoncoherentMemory()
    {
        memoryFlags &= ~static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostCoherent);
        recreatePoolAndUploads();
    }

    void useLargerAllocations()
    {
        maximumAllocationSize = 8192;
        recreatePoolAndUploads();
    }

    void recreatePoolAndUploads()
    {
        uploads.reset();
        pool.reset();
        pool = panda::core::vulkan::resources::MemoryPool::create(physical, device);
        uploads = std::make_unique<panda::core::vulkan::upload::UploadManager>(device, *pool, *submissions, 0);
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
        return destinationWithSize(512, usage);
    }

    auto destinationWithSize(std::uint64_t size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferDst)
        -> std::shared_ptr<panda::detail::BufferResource>
    {
        auto created = panda::detail::BufferResource::create(device, *pool, {.size = size, .usage = usage});
        return panda::expect(created, "Test destination could not be created");
    }

    auto texture(vk::Extent2D extent = {.width = 8, .height = 6},
                 vk::Format format = vk::Format::eR8G8B8A8Unorm,
                 vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        -> std::shared_ptr<panda::detail::TextureResource>
    {
        auto created =
            panda::detail::TextureResource::create(device, *pool, {.extent = extent, .format = format, .usage = usage});
        return panda::expect(created, "Test texture could not be created");
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<panda::core::vulkan::resources::MemoryPool> pool;
    std::unique_ptr<panda::detail::SubmissionQueue> submissions;
    std::unique_ptr<panda::core::vulkan::upload::UploadManager> uploads;
    std::unordered_map<VkBuffer, NativeBuffer> buffers;
    std::unordered_map<VkImage, NativeImage> images;
    std::unordered_map<VkImageView, VkImage> imageViews;
    std::unordered_map<VkDeviceMemory, std::vector<std::byte>> memory;
    std::vector<ObservedCopy> copies;
    std::vector<ObservedImageCopy> imageCopies;
    std::vector<VkMemoryBarrier2> barriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<char> copyOrder;
    std::vector<VkMappedMemoryRange> flushed;
    std::vector<std::uint64_t> signals;
    std::vector<char> destruction;
    VkMemoryPropertyFlags memoryFlags {static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal |
                                                                          vk::MemoryPropertyFlagBits::eHostVisible |
                                                                          vk::MemoryPropertyFlagBits::eHostCoherent)};
    VkDeviceSize maximumAllocationSize {4096};
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
    std::size_t failBufferCreationAt {0};
    std::size_t failFlushAt {0};
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
    auto otherPool = panda::core::vulkan::resources::MemoryPool::create(physical, device);
    auto other = panda::detail::BufferResource::create(device,
                                                       *otherPool,
                                                       {.size = 512, .usage = vk::BufferUsageFlagBits::eTransferDst});
    ASSERT_TRUE(other);
    EXPECT_FALSE(batch->write(*other, 0, payload));

    auto foreignTexture = panda::detail::TextureResource::create(
        device,
        *otherPool,
        {
            .extent = {.width = 8, .height = 6},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    });
    ASSERT_TRUE(foreignTexture);
    const auto textureBytes = std::vector<std::byte>(std::size_t {8} * 6 * 4);
    const auto region = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 8, .height = 6}
    };
    EXPECT_FALSE(batch->write(*foreignTexture, textureBytes, region));
    const auto foreignMesh =
        panda::detail::MeshResource::create(device,
                                            *otherPool,
                                            {.vertexCount = 3, .vertexStride = 4, .indices = std::nullopt});
    ASSERT_TRUE(foreignMesh);
    const auto vertexBytes = std::array {std::byte {1},
                                         std::byte {2},
                                         std::byte {3},
                                         std::byte {4},
                                         std::byte {5},
                                         std::byte {6},
                                         std::byte {7},
                                         std::byte {8},
                                         std::byte {9},
                                         std::byte {10},
                                         std::byte {11},
                                         std::byte {12}};
    EXPECT_FALSE(batch->write(*foreignMesh, vertexBytes, {}));
    EXPECT_EQ(bufferCreations, 3);
}

TEST_F(BufferUploadIntegration, MeshWriteCopiesVertexAndIndexStorageInOrder)
{
    const auto mesh = panda::detail::MeshResource::create(
        device,
        *pool,
        {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
    });
    ASSERT_TRUE(mesh);
    const auto vertexBytes = std::array {std::byte {1},
                                         std::byte {2},
                                         std::byte {3},
                                         std::byte {4},
                                         std::byte {5},
                                         std::byte {6},
                                         std::byte {7},
                                         std::byte {8},
                                         std::byte {9},
                                         std::byte {10},
                                         std::byte {11},
                                         std::byte {12}};
    const auto indexBytes =
        std::array {std::byte {0}, std::byte {1}, std::byte {1}, std::byte {2}, std::byte {2}, std::byte {0}};
    auto batch = uploads->createBatch();

    ASSERT_TRUE(batch->write(*mesh, vertexBytes, indexBytes));
    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(copies.size(), 2);
    EXPECT_EQ(copies.front().destination, (*mesh)->getVertexBuffer()->getNative());
    EXPECT_EQ(copies.front().bytes, (std::vector<std::byte> {vertexBytes.begin(), vertexBytes.end()}));
    EXPECT_EQ(copies.back().destination, (*mesh)->getIndexBuffer()->getNative());
    EXPECT_EQ(copies.back().bytes, (std::vector<std::byte> {indexBytes.begin(), indexBytes.end()}));
}

TEST_F(BufferUploadIntegration, NonIndexedMeshWriteCopiesOnlyVertexStorage)
{
    const auto mesh =
        panda::detail::MeshResource::create(device,
                                            *pool,
                                            {.vertexCount = 3, .vertexStride = 4, .indices = std::nullopt});
    ASSERT_TRUE(mesh);
    ASSERT_FALSE((*mesh)->getIndexBuffer());
    const auto vertexBytes = std::array {std::byte {1},
                                         std::byte {2},
                                         std::byte {3},
                                         std::byte {4},
                                         std::byte {5},
                                         std::byte {6},
                                         std::byte {7},
                                         std::byte {8},
                                         std::byte {9},
                                         std::byte {10},
                                         std::byte {11},
                                         std::byte {12}};
    const auto indexBytes = std::array<std::byte, 0> {};
    auto batch = uploads->createBatch();

    ASSERT_TRUE(batch->write(*mesh, vertexBytes, indexBytes));
    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().destination, (*mesh)->getVertexBuffer()->getNative());
    EXPECT_EQ(copies.front().bytes, (std::vector<std::byte> {vertexBytes.begin(), vertexBytes.end()}));
}

TEST_F(BufferUploadIntegration, FailedVertexBufferCreationDoesNotPublishMesh)
{
    failBufferCreationAt = 1;

    const auto mesh =
        panda::detail::MeshResource::create(device,
                                            *pool,
                                            {.vertexCount = 3, .vertexStride = 4, .indices = std::nullopt});

    ASSERT_FALSE(mesh);
    EXPECT_EQ(mesh.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(bufferCreations, 1);
    EXPECT_TRUE(buffers.empty());
}

TEST_F(BufferUploadIntegration, FailedIndexBufferCreationReleasesUnpublishedVertexBuffer)
{
    failBufferCreationAt = 2;

    const auto mesh = panda::detail::MeshResource::create(
        device,
        *pool,
        {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
    });

    ASSERT_FALSE(mesh);
    EXPECT_EQ(mesh.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(bufferCreations, 2);
    EXPECT_TRUE(buffers.empty());
}

TEST_F(BufferUploadIntegration, InvalidMeshWriteQueuesNoPartialData)
{
    const auto mesh = panda::detail::MeshResource::create(
        device,
        *pool,
        {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
    });
    ASSERT_TRUE(mesh);
    auto batch = uploads->createBatch();
    const auto shortVertices = std::array<std::byte, 11> {};
    const auto indices = std::array<std::byte, 6> {};
    const auto shortIndices = std::array<std::byte, 4> {};

    const auto invalid = batch->write(*mesh, shortVertices, indices);
    const auto invalidIndices = batch->write(*mesh, std::array<std::byte, 12> {}, shortIndices);

    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(invalidIndices);
    EXPECT_EQ(invalidIndices.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(bufferCreations, 2);
    EXPECT_FALSE(batch->submit());
    EXPECT_TRUE(copies.empty());
}

TEST_F(BufferUploadIntegration, SecondMeshStagingFailurePreservesOnlyEarlierBatchWrites)
{
    useNoncoherentMemory();
    auto target = destination();
    const auto mesh = panda::detail::MeshResource::create(
        device,
        *pool,
        {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
    });
    ASSERT_TRUE(mesh);
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    const auto vertexBytes = std::array {std::byte {1},
                                         std::byte {2},
                                         std::byte {3},
                                         std::byte {4},
                                         std::byte {5},
                                         std::byte {6},
                                         std::byte {7},
                                         std::byte {8},
                                         std::byte {9},
                                         std::byte {10},
                                         std::byte {11},
                                         std::byte {12}};
    const auto indexBytes =
        std::array {std::byte {0}, std::byte {1}, std::byte {1}, std::byte {2}, std::byte {2}, std::byte {0}};
    failFlushAt = flushed.size() + 2;

    const auto failed = batch->write(*mesh, vertexBytes, indexBytes);

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(batch->submit());
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().destination, target->getNative());
    EXPECT_EQ(copies.front().bytes, (std::vector<std::byte> {payload.begin(), payload.end()}));
}

TEST_F(BufferUploadIntegration, FirstMeshStagingFailurePreservesOnlyEarlierBatchWrites)
{
    useNoncoherentMemory();
    auto target = destination();
    const auto mesh = panda::detail::MeshResource::create(
        device,
        *pool,
        {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
    });
    ASSERT_TRUE(mesh);
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));
    const auto vertexBytes = std::array {std::byte {1},
                                         std::byte {2},
                                         std::byte {3},
                                         std::byte {4},
                                         std::byte {5},
                                         std::byte {6},
                                         std::byte {7},
                                         std::byte {8},
                                         std::byte {9},
                                         std::byte {10},
                                         std::byte {11},
                                         std::byte {12}};
    const auto indexBytes =
        std::array {std::byte {0}, std::byte {1}, std::byte {1}, std::byte {2}, std::byte {2}, std::byte {0}};
    failFlushAt = flushed.size() + 1;

    const auto failed = batch->write(*mesh, vertexBytes, indexBytes);

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().destination, target->getNative());
    EXPECT_EQ(copies.front().bytes, (std::vector<std::byte> {payload.begin(), payload.end()}));
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
    EXPECT_EQ(bufferCreations, 2);
    EXPECT_EQ(copies.front().sourceOffset, 0);
    EXPECT_EQ(copies.back().sourceOffset, payload.size());
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

TEST_F(BufferUploadIntegration, TextureRegionSnapshotsPaddedRowsAndRetainsImageThroughCompletion)
{
    auto target = texture();
    const auto image = target->getNative();
    const auto weak = std::weak_ptr {target};
    auto bytes = std::vector<std::byte>(28, std::byte {0x5a});
    const auto expected = bytes;
    const auto region = panda::TextureUploadRegion {
        .offset = vk::Offset2D {.x = 2,     .y = 1     },
        .extent = vk::Extent2D {.width = 3, .height = 2},
        .rowPitchBytes = 16
    };
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, bytes, region));
    std::ranges::fill(bytes, std::byte {0});
    target.reset();

    const auto submitted = batch->submit();
    ASSERT_TRUE(submitted);
    batch.reset();
    ASSERT_EQ(imageCopies.size(), 1);
    const auto& copy = imageCopies.front();
    EXPECT_EQ(copy.destination, image);
    EXPECT_EQ(copy.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EXPECT_EQ(copy.region.bufferRowLength, 4);
    EXPECT_EQ(copy.region.bufferImageHeight, 0);
    EXPECT_EQ(copy.region.imageOffset.x, 2);
    EXPECT_EQ(copy.region.imageOffset.y, 1);
    EXPECT_EQ(copy.region.imageExtent.width, 3);
    EXPECT_EQ(copy.region.imageExtent.height, 2);
    EXPECT_EQ(copy.bytes, expected);
    ASSERT_EQ(imageBarriers.size(), 2);
    EXPECT_EQ(imageBarriers.front().oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(imageBarriers.front().newLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EXPECT_EQ(imageBarriers.back().oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EXPECT_EQ(imageBarriers.back().newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_FALSE(weak.expired());
    ASSERT_TRUE(submissions->wait(*submitted, 100));
    uploads->collectCompleted();
    EXPECT_TRUE(weak.expired());
}

TEST_F(BufferUploadIntegration, MultipleTextureRegionsAndBuffersKeepWriteOrderAndSingleTransitions)
{
    auto target = texture(vk::Extent2D {.width = 8, .height = 4});
    auto buffer = destination();
    auto batch = uploads->createBatch();
    const auto left = std::vector<std::byte>(std::size_t {4} * 4 * 4, std::byte {0x11});
    const auto right = std::vector<std::byte>(std::size_t {4} * 4 * 4, std::byte {0x22});
    const auto leftRegion = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 4, .height = 4}
    };
    const auto rightRegion = panda::TextureUploadRegion {
        .offset = vk::Offset2D {.x = 4,     .y = 0     },
        .extent = vk::Extent2D {.width = 4, .height = 4}
    };
    ASSERT_TRUE(batch->write(buffer, 0, payload));
    ASSERT_TRUE(batch->write(target, left, leftRegion));
    ASSERT_TRUE(batch->write(target, right, rightRegion));

    ASSERT_TRUE(batch->submit());
    EXPECT_EQ(copyOrder, (std::vector<char> {'b', 'i', 'i'}));
    EXPECT_EQ(bufferCreations, 2);
    ASSERT_EQ(imageCopies.size(), 2);
    EXPECT_EQ(imageCopies.at(0).region.bufferOffset, 8);
    EXPECT_EQ(imageCopies.at(1).region.bufferOffset, 72);
    EXPECT_EQ(imageCopies.at(0).bytes, left);
    EXPECT_EQ(imageCopies.at(1).bytes, right);
    EXPECT_EQ(imageCopies.at(0).region.imageOffset.x, 0);
    EXPECT_EQ(imageCopies.at(1).region.imageOffset.x, 4);
    EXPECT_EQ(imageBarriers.size(), 2);
    EXPECT_EQ(imageBarriers.front().oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(imageBarriers.back().newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST_F(BufferUploadIntegration, InvalidTextureRegionsAndSourceLayoutsAreRejectedBeforeStaging)
{
    auto target = texture();
    auto batch = uploads->createBatch();
    const auto bytes = std::vector<std::byte>(32);
    const auto negative = panda::TextureUploadRegion {
        .offset = vk::Offset2D {.x = -1,    .y = 0     },
        .extent = vk::Extent2D {.width = 1, .height = 1}
    };
    const auto outOfBounds = panda::TextureUploadRegion {
        .offset = vk::Offset2D {.x = 7,     .y = 5     },
        .extent = vk::Extent2D {.width = 2, .height = 1}
    };
    const auto empty = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 0, .height = 1}
    };
    const auto unalignedPitch = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 3, .height = 2},
        .rowPitchBytes = 11
    };
    const auto shortPitch = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 3, .height = 2},
        .rowPitchBytes = 8
    };
    const auto pitched = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 3, .height = 2},
        .rowPitchBytes = 16
    };
    const auto shortSource = std::span<const std::byte> {bytes}.first(23);
    EXPECT_FALSE(batch->write(target, bytes, negative));
    EXPECT_FALSE(batch->write(target, bytes, outOfBounds));
    EXPECT_FALSE(batch->write(target, bytes, empty));
    EXPECT_FALSE(batch->write(target, bytes, unalignedPitch));
    EXPECT_FALSE(batch->write(target, bytes, shortPitch));
    EXPECT_FALSE(batch->write(target, shortSource, pitched));
    EXPECT_EQ(bufferCreations, 0);
    EXPECT_TRUE(imageCopies.empty());
}

TEST_F(BufferUploadIntegration, TextureWritesRequireSupportedFormatAndBothUsageFlags)
{
    const auto sampledOnly =
        texture(vk::Extent2D {.width = 8, .height = 6}, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled);
    const auto noSampled = texture(vk::Extent2D {.width = 8, .height = 6},
                                   vk::Format::eR8G8B8A8Unorm,
                                   vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment);
    const auto depth = texture(vk::Extent2D {.width = 8, .height = 6},
                               vk::Format::eD32Sfloat,
                               vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    auto batch = uploads->createBatch();
    const auto bytes = std::vector<std::byte>(4);
    const auto region = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 1, .height = 1}
    };

    EXPECT_FALSE(batch->write(sampledOnly, bytes, region));
    EXPECT_FALSE(batch->write(noSampled, bytes, region));
    EXPECT_FALSE(batch->write(depth, bytes, region));
    EXPECT_EQ(bufferCreations, 0);
}

TEST_F(BufferUploadIntegration, TextureReservationIsReleasedOnCancelAndCommittedOnSubmit)
{
    auto target = texture();
    const auto bytes = std::vector<std::byte>(std::size_t {8} * 6 * 4);
    auto cancelled = uploads->createBatch();
    const auto fullRegion = panda::TextureUploadRegion {.extent = target->getExtent()};
    ASSERT_TRUE(cancelled->write(target, bytes, fullRegion));
    auto conflicting = uploads->createBatch();
    EXPECT_FALSE(conflicting->write(target, bytes, fullRegion));
    conflicting.reset();
    cancelled.reset();

    auto committed = uploads->createBatch();
    ASSERT_TRUE(committed->write(target, bytes, fullRegion));
    ASSERT_TRUE(committed->submit());
    auto later = uploads->createBatch();
    EXPECT_FALSE(later->write(target, bytes, fullRegion));
}

TEST_F(BufferUploadIntegration, TextureReservationIsReleasedAfterStagingCreationFails)
{
    auto target = texture();
    const auto bytes = std::vector<std::byte>(std::size_t {8} * 6 * 4);
    const auto fullRegion = panda::TextureUploadRegion {.extent = target->getExtent()};
    auto failed = uploads->createBatch();
    bufferStatus = exhausted;
    const auto write = failed->write(target, bytes, fullRegion);
    ASSERT_FALSE(write);
    EXPECT_EQ(write.error().code, panda::ErrorCode::ResourceExhausted);
    bufferStatus = success;

    auto retry = uploads->createBatch();
    ASSERT_TRUE(retry->write(target, bytes, fullRegion));
}

TEST_F(BufferUploadIntegration, FailedLaterTextureWriteKeepsTheBatchReservation)
{
    useNoncoherentMemory();
    auto target = texture();
    const auto bytes = std::array {std::byte {1}, std::byte {2}, std::byte {3}, std::byte {4}};
    const auto firstRegion = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 1, .height = 1}
    };
    const auto secondRegion = panda::TextureUploadRegion {
        .offset = vk::Offset2D {.x = 1,     .y = 0     },
        .extent = vk::Extent2D {.width = 1, .height = 1}
    };
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, bytes, firstRegion));

    flushStatus = exhausted;
    EXPECT_FALSE(batch->write(target, bytes, secondRegion));
    auto conflicting = uploads->createBatch();
    EXPECT_FALSE(conflicting->write(target, bytes, secondRegion));
    conflicting.reset();

    flushStatus = success;
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(imageCopies.size(), 1);
    EXPECT_EQ(imageCopies.front().bytes, (std::vector<std::byte> {bytes.begin(), bytes.end()}));
}

TEST_F(BufferUploadIntegration, CompressedTextureUsesBlockRowsAndRequiresAlignedRegions)
{
    auto buffer = destination();
    auto target = texture(vk::Extent2D {.width = 7, .height = 7}, vk::Format::eBc1RgbUnormBlock);
    auto batch = uploads->createBatch();
    const auto bytes = std::vector<std::byte>(32, std::byte {0x5a});
    ASSERT_TRUE(batch->write(buffer, 0, payload));
    ASSERT_TRUE(batch->write(target,
                             bytes,
                             {
                                 .extent = vk::Extent2D {.width = 7, .height = 7},
                                 .rowPitchBytes = 16
    }));
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(imageCopies.size(), 1);
    EXPECT_EQ(imageCopies.front().region.bufferOffset, 8);
    EXPECT_EQ(imageCopies.front().bytes, bytes);
    EXPECT_EQ(imageCopies.front().region.bufferRowLength, 8);
    EXPECT_EQ(imageCopies.front().region.imageExtent.width, 7);
    EXPECT_EQ(imageCopies.front().region.imageExtent.height, 7);

    auto other = texture(vk::Extent2D {.width = 8, .height = 8}, vk::Format::eBc1RgbUnormBlock);
    auto invalid = uploads->createBatch();
    const auto block = std::vector<std::byte>(8, std::byte {0x33});
    EXPECT_FALSE(invalid->write(other,
                                block,
                                {
                                    .offset = vk::Offset2D {.x = 2,     .y = 0     },
                                    .extent = vk::Extent2D {.width = 4, .height = 4}
    }));
}

TEST_F(BufferUploadIntegration, TextureSourceOffsetAlignsForThreeByteTexels)
{
    constexpr auto format = vk::Format::eR8G8B8Unorm;
    EXPECT_EQ(vk::blockSize(format), 3U);
    auto buffer = destination();
    auto target = texture(vk::Extent2D {.width = 2, .height = 2}, format);
    auto batch = uploads->createBatch();
    const auto bytes = std::vector<std::byte>(12, std::byte {0x5a});
    ASSERT_TRUE(batch->write(buffer, 0, payload));
    ASSERT_TRUE(batch->write(target,
                             bytes,
                             {
                                 .extent = vk::Extent2D {.width = 2, .height = 2}
    }));

    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(imageCopies.size(), 1);
    EXPECT_EQ(imageCopies.front().region.bufferOffset, 12);
    EXPECT_EQ(imageCopies.front().bytes, bytes);
}

TEST_F(BufferUploadIntegration, SingleRowPitchUsesTheUnsignedVulkanRowLengthRange)
{
    auto target = texture();
    auto batch = uploads->createBatch();
    const auto bytes = std::array<std::byte, 4> {};
    const auto region = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 1, .height = 1},
        .rowPitchBytes = std::uint64_t {1}
           << 31U
    };

    ASSERT_TRUE(batch->write(target, bytes, region));
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(imageCopies.size(), 1);
    EXPECT_EQ(imageCopies.front().region.bufferRowLength, std::uint32_t {1} << 29U);
}

TEST_F(BufferUploadIntegration, TextureCopyRejectsRowLengthAndSourceSizeOverflow)
{
    const auto bytes = std::array {std::byte {1}, std::byte {2}, std::byte {3}, std::byte {4}};
    const auto rowLengthOverflow = texture();
    auto rowLengthBatch = uploads->createBatch();
    const auto excessiveRowPitch = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 1, .height = 1},
        .rowPitchBytes = (std::uint64_t {std::numeric_limits<std::uint32_t>::max()}
           + 1) * 4
    };
    const auto rowLengthResult = rowLengthBatch->write(rowLengthOverflow, bytes, excessiveRowPitch);
    ASSERT_FALSE(rowLengthResult);
    EXPECT_EQ(rowLengthResult.error().code, panda::ErrorCode::InvalidArgument);

    const auto maximumHeight = std::numeric_limits<std::uint32_t>::max();
    const auto sourceSizeOverflow = texture(vk::Extent2D {.width = 1, .height = maximumHeight});
    auto sourceSizeBatch = uploads->createBatch();
    const auto oversizedSourceLayout = panda::TextureUploadRegion {
        .extent = vk::Extent2D {.width = 1, .height = maximumHeight},
        .rowPitchBytes = std::uint64_t {maximumHeight}
           * 4
    };
    const auto sourceSizeResult = sourceSizeBatch->write(sourceSizeOverflow, bytes, oversizedSourceLayout);
    ASSERT_FALSE(sourceSizeResult);
    EXPECT_EQ(sourceSizeResult.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(bufferCreations, 0);
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
    auto target = destinationWithSize(3000);
    const auto bytes = std::vector<std::byte>(3000, std::byte {0x5a});
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, bytes));
    bufferStatus = exhausted;
    const auto failed = batch->write(target, 0, bytes);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    bufferStatus = success;
    ASSERT_TRUE(batch->submit());
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().offset, 0);
    EXPECT_EQ(copies.front().bytes, bytes);
}

TEST_F(BufferUploadIntegration, TextureAlignmentPastALargePageStartsANewPage)
{
    useLargerAllocations();
    auto buffer = destinationWithSize(5001);
    auto target = texture(vk::Extent2D {.width = 1, .height = 1}, vk::Format::eR8G8B8Unorm);
    const auto bufferBytes = std::vector<std::byte>(5001, std::byte {0x11});
    const auto textureBytes = std::array {std::byte {0x21}, std::byte {0x22}, std::byte {0x23}};
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(buffer, 0, bufferBytes));
    ASSERT_TRUE(batch->write(target,
                             textureBytes,
                             {
                                 .extent = vk::Extent2D {.width = 1, .height = 1}
    }));

    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(copies.front().sourceOffset, 0);
    EXPECT_EQ(copies.front().bytes, bufferBytes);
    ASSERT_EQ(imageCopies.size(), 1);
    EXPECT_EQ(imageCopies.front().region.bufferOffset, 0);
    EXPECT_EQ(imageCopies.front().bytes, (std::vector<std::byte> {textureBytes.begin(), textureBytes.end()}));
    EXPECT_EQ(bufferCreations, 3);
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 5001 + 4096);
}

TEST_F(BufferUploadIntegration, MappedStagingWritesValidateAndUseTheRequestedOffset)
{
    auto deviceBuffer = destination();
    EXPECT_FALSE(deviceBuffer->writeMapped(0, payload));

    auto staging = uploads->createStaging(8);
    ASSERT_TRUE(staging);
    EXPECT_FALSE((*staging)->writeMapped(9, payload));
    EXPECT_FALSE((*staging)->writeMapped(3, payload));
    EXPECT_FALSE((*staging)->writeMapped(0, {}));

    const auto bytes = std::array {std::byte {0x31}, std::byte {0x32}, std::byte {0x33}};
    ASSERT_TRUE((*staging)->writeMapped(5, bytes));
    const auto& native = buffers.at((*staging)->getNative());
    const auto mapped = std::span<const std::byte> {memory.at(native.memory)}.subspan(native.offset + 5, bytes.size());
    EXPECT_TRUE(std::ranges::equal(mapped, bytes));
}

TEST_F(BufferUploadIntegration, StagingPageGrowsForWritesThatDoNotFit)
{
    auto firstTarget = destinationWithSize(3000);
    auto secondTarget = destinationWithSize(3000);
    const auto firstBytes = std::vector<std::byte>(3000, std::byte {0x11});
    const auto secondBytes = std::vector<std::byte>(3000, std::byte {0x22});
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(firstTarget, 0, firstBytes));
    ASSERT_TRUE(batch->write(secondTarget, 0, secondBytes));

    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(copies.size(), 2);
    EXPECT_EQ(bufferCreations, 4);
    EXPECT_EQ(copies.at(0).sourceOffset, 0);
    EXPECT_EQ(copies.at(1).sourceOffset, 0);
    EXPECT_EQ(copies.at(0).bytes, firstBytes);
    EXPECT_EQ(copies.at(1).bytes, secondBytes);
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 8192);
}

TEST_F(BufferUploadIntegration, NoncoherentStagingFlushesWholeAtomsAndFailedFlushPublishesNoWrite)
{
    useNoncoherentMemory();
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
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 4096);
}

TEST_F(BufferUploadIntegration, FailedFlushWhileReusingPageLeavesItsCursorAndEarlierWritesIntact)
{
    useNoncoherentMemory();
    auto target = destination();
    auto batch = uploads->createBatch();
    ASSERT_TRUE(batch->write(target, 0, payload));

    flushStatus = exhausted;
    EXPECT_FALSE(batch->write(target, 16, payload));
    EXPECT_EQ(buffers.size(), 2);

    flushStatus = success;
    ASSERT_TRUE(batch->write(target, 32, payload));
    ASSERT_TRUE(batch->submit());

    ASSERT_EQ(copies.size(), 2);
    EXPECT_EQ(copies.at(0).sourceOffset, 0);
    EXPECT_EQ(copies.at(1).sourceOffset, payload.size());
    EXPECT_EQ(copies.at(0).bytes, (std::vector<std::byte> {payload.begin(), payload.end()}));
    EXPECT_EQ(copies.at(1).bytes, (std::vector<std::byte> {payload.begin(), payload.end()}));
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
    EXPECT_EQ(uploads->getStats().peakStagingBytes, 8192);
    completed = 1;
    ASSERT_TRUE(uploads->collect());
    EXPECT_EQ(uploads->getStats().pendingBatches, 1);
    EXPECT_EQ(uploads->getStats().pendingStagingBytes, 4096);
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

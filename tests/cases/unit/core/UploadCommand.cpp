// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "upload/UploadCommand.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadOperation.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::core::vulkan::upload::BufferUpload;
using panda::core::vulkan::upload::UploadCommand;
using panda::core::vulkan::upload::UploadOperation;
using panda::detail::BufferResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

class UploadCommandTest : public testing::Test
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
                properties->properties.limits.maxMemoryAllocationCount = 8;
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

    auto makeBuffer(std::uint64_t size) -> std::shared_ptr<BufferResource>
    {
        const auto description =
            panda::BufferDesc {.size = size,
                               .usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
                               .memory = panda::BufferMemory::Upload};
        // GMock's WillOnce wrapper triggers a false-positive leak report in clang-analyzer.
        EXPECT_CALL(buffers, create(Ref(device), Ref(*pool), testing::Field(&panda::BufferDesc::size, size)))
            .Times(1)
            .WillRepeatedly(Return(panda::Result<void> {}));
        auto created = BufferResource::create(device, *pool, description);
        EXPECT_TRUE(created);
        return created ? std::move(*created) : nullptr;
    }

    auto expectPoolCreation() -> VkCommandPool
    {
        auto* const poolHandle = fakeVulkanHandle<VkCommandPool>(21);
        EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
            .WillOnce([poolHandle](VkDevice,
                                   const VkCommandPoolCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkCommandPool* output) {
                EXPECT_EQ(info->flags, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
                EXPECT_EQ(info->queueFamilyIndex, 5);
                *output = poolHandle;
                return VK_SUCCESS;
            });
        return poolHandle;
    }

    auto expectCommandBufferAllocation(VkCommandPool poolHandle) -> VkCommandBuffer
    {
        auto* const command = fakeVulkanHandle<VkCommandBuffer>(22);
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull()))
            .WillOnce(
                [command, poolHandle](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
                    EXPECT_EQ(info->commandPool, poolHandle);
                    EXPECT_EQ(info->level, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
                    EXPECT_EQ(info->commandBufferCount, 1);
                    *output = command;
                    return VK_SUCCESS;
                });
        return command;
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocationMock;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::BufferResourceMock> buffers;
    panda::test::ScopedMock<panda::test::TextureResourceMock> textures;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<MemoryPool> pool;
};
}

TEST_F(UploadCommandTest, PreservesCommandPoolCreationFailure)
{
    const auto failure = VK_ERROR_OUT_OF_HOST_MEMORY;
    EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull())).WillOnce(Return(failure));

    const auto command = UploadCommand::create(device, 5, {}, {});

    ASSERT_FALSE(command);
    const auto& native = command.error().native;
    if (!native.has_value())
    {
        FAIL() << "Expected a native Vulkan error";
        return;
    }
    EXPECT_EQ(native->code, static_cast<std::int64_t>(failure));
}

TEST_F(UploadCommandTest, DestroysTheCommandPoolWhenCommandBufferAllocationFails)
{
    auto* const poolHandle = expectPoolCreation();
    const auto failure = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull())).WillOnce(Return(failure));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, poolHandle, _));

    const auto command = UploadCommand::create(device, 5, {}, {});

    ASSERT_FALSE(command);
    const auto& native = command.error().native;
    if (!native.has_value())
    {
        FAIL() << "Expected a native Vulkan error";
        return;
    }
    EXPECT_EQ(native->code, static_cast<std::int64_t>(failure));
}

TEST_F(UploadCommandTest, DestroysTheCommandPoolWhenRecordingCannotBegin)
{
    auto* const poolHandle = expectPoolCreation();
    static_cast<void>(expectCommandBufferAllocation(poolHandle));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull())).WillOnce(Return(VK_ERROR_DEVICE_LOST));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, poolHandle, _));

    const auto command = UploadCommand::create(device, 5, {}, {});

    ASSERT_FALSE(command);
    EXPECT_EQ(command.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(UploadCommandTest, DestroysTheCommandPoolWhenRecordingCannotEnd)
{
    auto* const poolHandle = expectPoolCreation();
    auto* const commandHandle = expectCommandBufferAllocation(poolHandle);
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(commandHandle, NotNull())).WillOnce(Return(VK_SUCCESS));
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(commandHandle, NotNull())).Times(1);
    EXPECT_CALL(vulkan, vkEndCommandBuffer(commandHandle)).WillOnce(Return(VK_ERROR_DEVICE_LOST));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, poolHandle, _));

    const auto command = UploadCommand::create(device, 5, {}, {});

    ASSERT_FALSE(command);
    EXPECT_EQ(command.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(UploadCommandTest, RecordsBufferCopyBetweenTheSuppliedResources)
{
    auto source = makeBuffer(64);
    auto destination = makeBuffer(128);
    auto* const poolHandle = expectPoolCreation();
    auto* const commandHandle = expectCommandBufferAllocation(poolHandle);
    auto* const sourceHandle = fakeVulkanHandle<VkBuffer>(23);
    auto* const destinationHandle = fakeVulkanHandle<VkBuffer>(24);
    const auto stagingOffset = std::uint64_t {16};
    const auto destinationOffset = std::uint64_t {32};
    const auto copySize = std::uint64_t {48};
    const auto writes = std::array {UploadOperation {BufferUpload {.staging = source,
                                                                   .stagingOffset = stagingOffset,
                                                                   .destination = destination,
                                                                   .offset = destinationOffset,
                                                                   .size = copySize}}};
    const auto pages = std::array {source};
    const auto beginInfo = testing::Truly([](const VkCommandBufferBeginInfo* info) {
        return info != nullptr && info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    });
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(commandHandle, beginInfo)).WillOnce(Return(VK_SUCCESS));
    const testing::Sequence recording;
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(commandHandle, NotNull()))
        .InSequence(recording)
        .WillOnce([](VkCommandBuffer, const VkDependencyInfo* dependency) {
            EXPECT_EQ(dependency->memoryBarrierCount, 1);
            EXPECT_EQ(dependency->imageMemoryBarrierCount, 0);
        });
    EXPECT_CALL(buffers, getNative(Ref(*source))).WillOnce(Return(vk::Buffer {sourceHandle}));
    EXPECT_CALL(buffers, getNative(Ref(*destination))).WillOnce(Return(vk::Buffer {destinationHandle}));
    EXPECT_CALL(vulkan, vkCmdCopyBuffer(commandHandle, sourceHandle, destinationHandle, 1, NotNull()))
        .InSequence(recording)
        .WillOnce([&](VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t count, const VkBufferCopy* region) {
            ASSERT_EQ(count, 1);
            EXPECT_EQ(region->srcOffset, stagingOffset);
            EXPECT_EQ(region->dstOffset, destinationOffset);
            EXPECT_EQ(region->size, copySize);
        });
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(commandHandle, NotNull()))
        .InSequence(recording)
        .WillOnce([](VkCommandBuffer, const VkDependencyInfo* dependency) {
            EXPECT_EQ(dependency->memoryBarrierCount, 1);
            EXPECT_EQ(dependency->imageMemoryBarrierCount, 0);
        });
    EXPECT_CALL(vulkan, vkEndCommandBuffer(commandHandle)).WillOnce(Return(VK_SUCCESS));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(_, poolHandle, _));
    EXPECT_CALL(buffers, getReservedSize(Ref(*source))).WillOnce(Return(64));

    auto created = UploadCommand::create(device, 5, writes, pages);

    ASSERT_TRUE(created);
    EXPECT_EQ((*created)->getCommand(), vk::CommandBuffer {commandHandle});
    EXPECT_EQ((*created)->getStagingSize(), 64);
}

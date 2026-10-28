// clang-format off
#ifdef VK_USE_PLATFORM_WIN32_KHR
#    include <Windows.h> // IWYU pragma: keep
#    include <winnt.h>
#endif
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "external/ExternalContext.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#ifdef VK_USE_PLATFORM_WIN32_KHR
#    include <vulkan/vulkan_win32.h>
#endif
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalMemoryHandle.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <utility>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::BufferResource;
using panda::detail::ExternalContext;
using panda::detail::SubmissionQueue;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

constexpr auto success = VK_SUCCESS;

auto memoryHandleType() -> vk::ExternalMemoryHandleTypeFlagBits
{
#ifdef _WIN32
    return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
    return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
}

auto semaphoreHandleType() -> vk::ExternalSemaphoreHandleTypeFlagBits
{
#ifdef _WIN32
    return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
    return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
}

auto publicMemoryHandleType() -> panda::ExternalMemoryHandleType
{
#ifdef _WIN32
    return panda::ExternalMemoryHandleType::OpaqueWin32;
#else
    return panda::ExternalMemoryHandleType::OpaqueFd;
#endif
}

auto publicSemaphoreHandleType() -> panda::ExternalSemaphoreHandleType
{
#ifdef _WIN32
    return panda::ExternalSemaphoreHandleType::OpaqueWin32;
#else
    return panda::ExternalSemaphoreHandleType::OpaqueFd;
#endif
}

void expectBarrierSynchronization(const VkBufferMemoryBarrier2& barrier, bool release)
{
    const auto access =
        static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    EXPECT_EQ(barrier.srcStageMask,
              release ? static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands)
                      : static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eNone));
    EXPECT_EQ(barrier.srcAccessMask, release ? access : VkAccessFlags2 {});
    EXPECT_EQ(barrier.dstStageMask,
              release ? static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eNone)
                      : static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eAllCommands));
    EXPECT_EQ(barrier.dstAccessMask, release ? VkAccessFlags2 {} : access);
}

void expectBarrierOwnership(const VkBufferMemoryBarrier2& barrier, bool release)
{
    EXPECT_EQ(barrier.srcQueueFamilyIndex, release ? 0U : VK_QUEUE_FAMILY_EXTERNAL);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, release ? VK_QUEUE_FAMILY_EXTERNAL : 0U);
}

void expectBarrierRange(const VkBufferMemoryBarrier2& barrier, VkBuffer buffer)
{
    EXPECT_EQ(barrier.buffer, buffer);
    EXPECT_EQ(barrier.offset, 0U);
    EXPECT_EQ(barrier.size, 80U);
}

void expectTransferBarrier(const VkDependencyInfo* info, VkBuffer buffer, bool release)
{
    ASSERT_EQ(info->bufferMemoryBarrierCount, 1U);
    ASSERT_NE(info->pBufferMemoryBarriers, nullptr);
    const auto& barrier = *info->pBufferMemoryBarriers;
    expectBarrierSynchronization(barrier, release);
    expectBarrierOwnership(barrier, release);
    expectBarrierRange(barrier, buffer);
}

class ExternalContextTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(physicalHandle, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                output->memoryTypeCount = 1;
                output->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                output->memoryHeapCount = 1;
                output->memoryHeaps[0].size = 4096;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(physicalHandle, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                const auto* const chain = static_cast<const VkBaseOutStructure*>(output->pNext);
                if (chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)
                {
                    auto* const identity = static_cast<VkPhysicalDeviceIDProperties*>(output->pNext);
                    identity->deviceUUID[0] = 1;
                    return;
                }
                output->properties.limits.maxMemoryAllocationCount = 8;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* const maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        pool = MemoryPool::create(physical, device);

        EXPECT_CALL(submissionQueueMock, construct(Ref(device), 0));
        submissions = std::make_unique<SubmissionQueue>(device, 0);
    }

    void TearDown() override
    {
        pool.reset();
        submissions.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto makeContext(std::optional<panda::core::vulkan::upload::UploadManager>& uploads) -> ExternalContext
    {
        return ExternalContext {
            ExternalContext::CreateInfo {
                                         .physicalDevice = physical,
                                         .device = device,
                                         .pool = *pool,
                                         .submissions = std::ref(*submissions),
                                         .uploads = uploads,
                                         .queueFamily = 0,
                                         .support = panda::detail::ExternalSupport {
                    .deviceExtensions = {},
                    .memoryHandleType = memoryHandleType(),
                    .publicHandleType = publicMemoryHandleType(),
                    .semaphoreHandleType = std::pair {semaphoreHandleType(), publicSemaphoreHandleType()}}}
        };
    }

    void expectExternalHandleExports(VkDeviceMemory memory, VkSemaphore semaphore)
    {
#ifdef _WIN32
        std::ignore = memory;
        std::ignore = semaphore;
        EXPECT_CALL(vulkan, vkGetMemoryWin32HandleKHR(deviceHandle, NotNull(), NotNull()))
            .WillOnce([](VkDevice, const VkMemoryGetWin32HandleInfoKHR* info, HANDLE* output) {
                EXPECT_EQ(info->handleType, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT);
                *output = nullptr;
                return success;
            });
        EXPECT_CALL(vulkan, vkGetSemaphoreWin32HandleKHR(deviceHandle, NotNull(), NotNull()))
            .WillOnce([](VkDevice, const VkSemaphoreGetWin32HandleInfoKHR* info, HANDLE* output) {
                EXPECT_EQ(info->handleType, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT);
                *output = nullptr;
                return success;
            });
#else
        EXPECT_CALL(vulkan, vkGetMemoryFdKHR(deviceHandle, NotNull(), NotNull()))
            .WillOnce([memory](VkDevice, const VkMemoryGetFdInfoKHR* info, int* output) {
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->handleType, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
                *output = -1;
                return success;
            });
        EXPECT_CALL(vulkan, vkGetSemaphoreFdKHR(deviceHandle, NotNull(), NotNull()))
            .WillOnce([semaphore](VkDevice, const VkSemaphoreGetFdInfoKHR* info, int* output) {
                EXPECT_EQ(info->semaphore, semaphore);
                EXPECT_EQ(info->handleType, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
                *output = -1;
                return success;
            });
#endif
    }

    void expectBufferResourceCreation(const panda::BufferDesc& description, VkDeviceMemory memory)
    {
        EXPECT_CALL(bufferResourceMock, createExportable(Ref(device), Ref(*pool), _, memoryHandleType()))
            .WillOnce([description](const vk::raii::Device&,
                                    MemoryPool&,
                                    const panda::BufferDesc& requested,
                                    vk::ExternalMemoryHandleTypeFlagBits) -> panda::Result<void> {
                EXPECT_EQ(requested.size, description.size);
                EXPECT_EQ(requested.usage, description.usage);
                return {};
            });
        EXPECT_CALL(bufferResourceMock, getReservedSize(_)).WillOnce([](const BufferResource&) {
            return 128U;
        });
        EXPECT_CALL(bufferResourceMock, getNativeMemory(_)).WillOnce([memory](const BufferResource&) {
            return vk::DeviceMemory {memory};
        });
    }

    void expectSemaphoreCreation(VkSemaphore semaphore)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([semaphore](VkDevice,
                                  const VkSemaphoreCreateInfo* info,
                                  const VkAllocationCallbacks*,
                                  VkSemaphore* output) {
                const auto* const timeline = static_cast<const VkSemaphoreTypeCreateInfo*>(info->pNext);
                EXPECT_NE(timeline, nullptr);
                if (timeline == nullptr)
                {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                EXPECT_EQ(timeline->semaphoreType, VK_SEMAPHORE_TYPE_TIMELINE);
                const auto* const exported = static_cast<const VkExportSemaphoreCreateInfo*>(timeline->pNext);
                EXPECT_NE(exported, nullptr);
                if (exported == nullptr)
                {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
#ifdef _WIN32
                EXPECT_EQ(exported->handleTypes, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT);
#else
                EXPECT_EQ(exported->handleTypes, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
#endif
                *output = semaphore;
                return success;
            });
        EXPECT_CALL(vulkan, vkDestroySemaphore(deviceHandle, semaphore, nullptr));
    }

    auto createExternalBuffer(ExternalContext& context,
                              const panda::BufferDesc& description,
                              VkDeviceMemory memory,
                              VkSemaphore semaphore) -> panda::Result<panda::ExternalBuffer>
    {
        expectBufferResourceCreation(description, memory);
        expectSemaphoreCreation(semaphore);
        expectExternalHandleExports(memory, semaphore);
        return context.createBuffer(description);
    }

    void expectCommandAllocation(VkCommandPool commandPool, VkCommandBuffer command)
    {
        EXPECT_CALL(vulkan, vkCreateCommandPool(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([commandPool](VkDevice,
                                    const VkCommandPoolCreateInfo* info,
                                    const VkAllocationCallbacks*,
                                    VkCommandPool* output) {
                EXPECT_EQ(info->queueFamilyIndex, 0U);
                EXPECT_EQ(info->flags, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
                *output = commandPool;
                return success;
            })
            .RetiresOnSaturation();
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(deviceHandle, NotNull(), NotNull()))
            .WillOnce(
                [commandPool, command](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
                    EXPECT_EQ(info->commandPool, commandPool);
                    EXPECT_EQ(info->level, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
                    EXPECT_EQ(info->commandBufferCount, 1U);
                    *output = command;
                    return success;
                })
            .RetiresOnSaturation();
    }

    void expectCommandRecording(VkCommandBuffer command, VkBuffer buffer, bool release)
    {
        EXPECT_CALL(vulkan, vkBeginCommandBuffer(command, NotNull()))
            .WillOnce([](VkCommandBuffer, const VkCommandBufferBeginInfo* info) {
                EXPECT_EQ(info->flags, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
                return success;
            })
            .RetiresOnSaturation();
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(command, NotNull()))
            .WillOnce([buffer, release](VkCommandBuffer, const VkDependencyInfo* info) {
                expectTransferBarrier(info, buffer, release);
            })
            .RetiresOnSaturation();
        EXPECT_CALL(vulkan, vkEndCommandBuffer(command)).WillOnce(Return(success)).RetiresOnSaturation();
    }

    void expectTransferCommand(VkCommandPool commandPool, VkCommandBuffer command, VkBuffer buffer, bool release)
    {
        expectCommandAllocation(commandPool, command);
        expectCommandRecording(command, buffer, release);
        EXPECT_CALL(vulkan, vkDestroyCommandPool(deviceHandle, commandPool, nullptr));
    }

    void expectExternalHandoff(VkCommandPool releasePool,
                               VkCommandBuffer releaseCommand,
                               VkCommandPool acquirePool,
                               VkCommandBuffer acquireCommand,
                               VkBuffer buffer,
                               vk::Semaphore semaphore)
    {
        EXPECT_CALL(bufferResourceMock, getNative(_)).Times(2).WillRepeatedly(Return(vk::Buffer {buffer}));
        EXPECT_CALL(bufferResourceMock, getSize(_)).Times(2).WillRepeatedly(Return(80));
        expectTransferCommand(acquirePool, acquireCommand, buffer, false);
        expectTransferCommand(releasePool, releaseCommand, buffer, true);
        EXPECT_CALL(submissionQueueMock, submitExternalSignal(vk::CommandBuffer {releaseCommand}, semaphore, 1))
            .WillOnce(Return(panda::Result<std::uint64_t> {1}));
        EXPECT_CALL(submissionQueueMock, getPoint(1)).WillOnce(Return(panda::CompletionPoint {}));
        EXPECT_CALL(submissionQueueMock, poll(_)).WillOnce(Return(panda::Result<bool> {false}));
        EXPECT_CALL(submissionQueueMock, submitExternalWait(vk::CommandBuffer {acquireCommand}, semaphore, 2))
            .WillOnce(Return(panda::Result<std::uint64_t> {2}));
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::BufferResourceMock> bufferResourceMock;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissionQueueMock;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    VkPhysicalDevice physicalHandle {fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::PhysicalDevice physical {instance, physicalHandle};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::unique_ptr<SubmissionQueue> submissions;
};
}

TEST_F(ExternalContextTest, ReleaseAndAcquireRecordOwnershipBarriersAndTimelineSubmissions)
{
    auto* const nativeBuffer = fakeVulkanHandle<VkBuffer>(40);
    auto* const nativeMemory = fakeVulkanHandle<VkDeviceMemory>(41);
    auto* const nativeSemaphore = fakeVulkanHandle<VkSemaphore>(42);
    const auto description = panda::BufferDesc {.size = 80, .usage = vk::BufferUsageFlagBits::eStorageBuffer};
    auto uploads = std::optional<panda::core::vulkan::upload::UploadManager> {};
    auto externalContext = makeContext(uploads);
    auto created = createExternalBuffer(externalContext, description, nativeMemory, nativeSemaphore);
    ASSERT_TRUE(created);
    EXPECT_TRUE(created->isValid());
    EXPECT_TRUE(created->hasSemaphore());
    auto buffer = created->getBuffer();
    const auto semaphore = vk::Semaphore {nativeSemaphore};
    expectExternalHandoff(fakeVulkanHandle<VkCommandPool>(50),
                          fakeVulkanHandle<VkCommandBuffer>(51),
                          fakeVulkanHandle<VkCommandPool>(52),
                          fakeVulkanHandle<VkCommandBuffer>(53),
                          nativeBuffer,
                          semaphore);

    const auto released = externalContext.release(buffer);
    ASSERT_TRUE(released);
    EXPECT_EQ(*released, 1U);
    const auto resource = BufferResource::getResource(buffer);
    EXPECT_TRUE(resource->isExternallyOwned());
    EXPECT_EQ(resource->getExternalTimelineValue(), 1U);

    const auto duplicate = externalContext.release(buffer);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, panda::ErrorCode::InvalidArgument);
    const auto wrongValue = externalContext.acquire(buffer, 1);
    ASSERT_FALSE(wrongValue);
    EXPECT_EQ(wrongValue.error().code, panda::ErrorCode::InvalidArgument);

    const auto acquired = externalContext.acquire(buffer, 2);
    ASSERT_TRUE(acquired);
    EXPECT_FALSE(resource->isExternallyOwned());
    EXPECT_EQ(resource->getExternalTimelineValue(), 2U);
}

TEST_F(ExternalContextTest, FailedReleaseSubmissionKeepsVulkanOwnershipAndTimelineValue)
{
    auto* const nativeBuffer = fakeVulkanHandle<VkBuffer>(60);
    auto* const nativeMemory = fakeVulkanHandle<VkDeviceMemory>(61);
    auto* const nativeSemaphore = fakeVulkanHandle<VkSemaphore>(62);
    const auto description = panda::BufferDesc {.size = 80, .usage = vk::BufferUsageFlagBits::eStorageBuffer};
    auto uploads = std::optional<panda::core::vulkan::upload::UploadManager> {};
    auto externalContext = makeContext(uploads);
    auto created = createExternalBuffer(externalContext, description, nativeMemory, nativeSemaphore);
    ASSERT_TRUE(created);
    auto buffer = created->getBuffer();
    auto* const commandPool = fakeVulkanHandle<VkCommandPool>(63);
    auto* const command = fakeVulkanHandle<VkCommandBuffer>(64);
    EXPECT_CALL(bufferResourceMock, getNative(_)).WillOnce(Return(vk::Buffer {nativeBuffer}));
    EXPECT_CALL(bufferResourceMock, getSize(_)).WillOnce(Return(80));
    expectTransferCommand(commandPool, command, nativeBuffer, true);
    EXPECT_CALL(submissionQueueMock,
                submitExternalSignal(vk::CommandBuffer {command}, vk::Semaphore {nativeSemaphore}, 1))
        .WillOnce([](vk::CommandBuffer, vk::Semaphore, std::uint64_t) -> panda::Result<std::uint64_t> {
            return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "injected submission failure")};
        });

    const auto released = externalContext.release(buffer);
    ASSERT_FALSE(released);
    EXPECT_EQ(released.error().code, panda::ErrorCode::BackendFailure);
    const auto resource = BufferResource::getResource(buffer);
    EXPECT_FALSE(resource->isExternallyOwned());
    EXPECT_EQ(resource->getExternalTimelineValue(), 0U);
}

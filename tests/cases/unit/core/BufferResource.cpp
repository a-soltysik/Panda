// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "BufferResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>
#include <tuple>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::core::vulkan::resources::MemoryRequest;
using panda::detail::BufferResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto validDescription(panda::BufferMemory memory = panda::BufferMemory::Device) -> panda::BufferDesc
{
    return {.size = 80, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = memory};
}

class BufferResourceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = 4096, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.maxMemoryAllocationCount = 8;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
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

    void expectBufferCreated(VkBuffer buffer, std::uint64_t size = 80, VkResult createResult = success)
    {
        EXPECT_CALL(vulkan, vkCreateBuffer(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([buffer, size, createResult](VkDevice,
                                                   const VkBufferCreateInfo* info,
                                                   const VkAllocationCallbacks*,
                                                   VkBuffer* output) {
                EXPECT_EQ(info->size, size);
                EXPECT_EQ(info->usage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                EXPECT_EQ(info->sharingMode, VK_SHARING_MODE_EXCLUSIVE);
                if (createResult == success)
                {
                    *output = buffer;
                }
                return createResult;
            });
    }

    void expectRequirements(VkBuffer buffer)
    {
        EXPECT_CALL(vulkan, vkGetBufferMemoryRequirements2(deviceHandle, NotNull(), NotNull()))
            .WillOnce([buffer](VkDevice, const VkBufferMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                EXPECT_EQ(info->buffer, buffer);
                output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 1};
                auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(dedicated, nullptr);
                dedicated->requiresDedicatedAllocation = vk::False;
            });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocationMock;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::array<std::byte, 128> mapped {};
};
}

TEST_F(BufferResourceTest, RejectsInvalidDescriptionsBeforeCallingDependencies)
{
    const auto empty = BufferResource::create(
        device,
        *pool,
        {.size = 0, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = panda::BufferMemory::Device});
    const auto zeroUsage =
        BufferResource::create(device, *pool, {.size = 80, .usage = {}, .memory = panda::BufferMemory::Device});

    ASSERT_FALSE(empty);
    ASSERT_FALSE(zeroUsage);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(zeroUsage.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(BufferResourceTest, PreservesNativeCreationFailureWithoutAdoptingTheOutput)
{
    expectBufferCreated(fakeVulkanHandle<VkBuffer>(20), 80, exhausted);

    const auto created = BufferResource::create(device, *pool, validDescription());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(BufferResourceTest, ReleasesTheNativeBufferWhenAllocationFails)
{
    auto* const buffer = fakeVulkanHandle<VkBuffer>(20);
    expectBufferCreated(buffer);
    expectRequirements(buffer);
    EXPECT_CALL(vulkan, vkDestroyBuffer(deviceHandle, buffer, nullptr));
    EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
        .WillOnce(Return(panda::Result<std::uint64_t> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled pool failure")}}));

    const auto created = BufferResource::create(device, *pool, validDescription());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(BufferResourceTest, DestroysTheBufferBeforeReleasingItsAllocationAfterBindFailure)
{
    auto* const buffer = fakeVulkanHandle<VkBuffer>(20);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(21);
    const auto range = panda::core::vulkan::resources::MemoryRange {.offset = 64, .size = 128};
    expectBufferCreated(buffer);
    expectRequirements(buffer);
    EXPECT_CALL(poolMock, allocate(Ref(*pool), _)).WillOnce(Return(std::uint64_t {7}));
    EXPECT_CALL(allocationMock, getMemory(7)).WillOnce(Return(vk::DeviceMemory {memory}));
    EXPECT_CALL(allocationMock, getRange(7)).WillOnce(Return(range));
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkBindBufferMemory2(deviceHandle, 1, NotNull()))
        .InSequence(cleanup)
        .WillOnce([](VkDevice, std::uint32_t, const VkBindBufferMemoryInfo* info) {
            EXPECT_EQ(info->buffer, fakeVulkanHandle<VkBuffer>(20));
            EXPECT_EQ(info->memory, fakeVulkanHandle<VkDeviceMemory>(21));
            EXPECT_EQ(info->memoryOffset, 64);
            return exhausted;
        });
    EXPECT_CALL(vulkan, vkDestroyBuffer(deviceHandle, buffer, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 7)).InSequence(cleanup);

    const auto created = BufferResource::create(device, *pool, validDescription());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(BufferResourceTest, WritesOnlyValidUploadRangesAndPropagatesMappedFlushResults)
{
    auto* const buffer = fakeVulkanHandle<VkBuffer>(20);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(21);
    const auto range = panda::core::vulkan::resources::MemoryRange {.offset = 0, .size = 128};
    expectBufferCreated(buffer);
    expectRequirements(buffer);
    EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
        .WillOnce([&](MemoryPool&, const MemoryRequest& request) -> panda::Result<std::uint64_t> {
            EXPECT_EQ(request.requiredProperties, vk::MemoryPropertyFlagBits::eHostVisible);
            EXPECT_EQ(request.preferredProperties, vk::MemoryPropertyFlagBits::eHostCoherent);
            return 8;
        });
    EXPECT_CALL(allocationMock, getMemory(8)).WillOnce(Return(vk::DeviceMemory {memory}));
    EXPECT_CALL(allocationMock, getRange(8)).WillRepeatedly(Return(range));
    EXPECT_CALL(vulkan, vkBindBufferMemory2(deviceHandle, 1, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    EXPECT_CALL(allocationMock, getMappedBytes(8)).WillOnce(Return(std::span {mapped}));
    EXPECT_CALL(allocationMock, flush(8, 4, 3))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::DeviceLost, "Controlled flush failure")}}));
    EXPECT_CALL(vulkan, vkDestroyBuffer(deviceHandle, buffer, nullptr));
    EXPECT_CALL(poolMock, release(Ref(*pool), 8));

    auto created = BufferResource::create(device, *pool, validDescription(panda::BufferMemory::Upload));
    ASSERT_TRUE(created);
    EXPECT_EQ((*created)->getSize(), 80);
    EXPECT_EQ((*created)->getUsage(), vk::BufferUsageFlagBits::eStorageBuffer);
    EXPECT_EQ((*created)->getReservedSize(), 128);
    const auto copied = (*created)->writeMapped(4, std::array {std::byte {1}, std::byte {2}, std::byte {3}});
    ASSERT_FALSE(copied);
    EXPECT_EQ(copied.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_EQ(mapped.at(4), std::byte {1});
    EXPECT_EQ(mapped.at(6), std::byte {3});
}

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "resources/memory/MemoryPool.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <tuple>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/MemoryBlockMock.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::core::vulkan::resources::MemoryRange;
using panda::core::vulkan::resources::MemoryRequest;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto mebibyte = std::uint64_t {1024} * 1024;

auto requestFor(std::uint64_t size = 64) -> MemoryRequest
{
    return MemoryRequest {
        .requirements = {.size = size, .alignment = 16, .memoryTypeBits = 1},
        .resource = {.buffer = vk::Buffer {fakeVulkanHandle<VkBuffer>(20)}},
        .exportHandleTypes = {},
        .requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
        .preferredProperties = {}
    };
}

class MemoryPoolTest : public testing::Test
{
protected:
    void SetUp() override
    {
        memoryProperties.memoryTypeCount = 2;
        memoryProperties.memoryTypes[0] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
            .heapIndex = 0};
        memoryProperties.memoryTypes[1] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostVisible),
            .heapIndex = 0};
        memoryProperties.memoryHeapCount = 1;
        memoryProperties.memoryHeaps[0] = {.size = 128 * mebibyte, .flags = 0};
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([this](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                *output = memoryProperties;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.maxMemoryAllocationCount = 8;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 128 * mebibyte;
            });
        pool = MemoryPool::create(physical, device);
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void createOrdinaryBlock(std::uint64_t size, MemoryRange range)
    {
        EXPECT_CALL(blocks, create(_, _, size, 0)).WillOnce(Return(panda::Result<void> {}));
        EXPECT_CALL(blocks, allocate(_, testing::_, testing::_)).WillOnce(Return(range));
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryBlockMock> blocks;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    std::unique_ptr<MemoryPool> pool;
};
}

TEST_F(MemoryPoolTest, RejectsInvalidRequestsWithoutCreatingABlock)
{
    auto request = requestFor(0);

    const auto allocation = pool->allocate(request);

    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MemoryPoolTest, SelectsACompatibleTypeAndPublishesTheReturnedRange)
{
    constexpr auto capacity = 64 * mebibyte;
    constexpr auto range = MemoryRange {.offset = 128, .size = 64};
    createOrdinaryBlock(capacity, range);
    EXPECT_CALL(blocks, getMemory(_)).WillOnce(Return(vk::DeviceMemory {fakeVulkanHandle<VkDeviceMemory>(4)}));
    EXPECT_CALL(blocks, release(_, range)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(blocks, getFreeSize(_)).WillRepeatedly(Return(capacity));
    EXPECT_CALL(blocks, getCapacity(_)).WillRepeatedly(Return(capacity));

    auto allocation = pool->allocate(requestFor());

    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange(), range);
    EXPECT_EQ(allocation->getMemory(), vk::DeviceMemory {fakeVulkanHandle<VkDeviceMemory>(4)});
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().committedBytes, capacity);
    EXPECT_EQ(pool->getStats().liveBytes, range.size);
    allocation->reset();
    EXPECT_EQ(pool->getStats().liveBytes, 0);
}

TEST_F(MemoryPoolTest, UsesTheRequiredMemoryTypeAndExactDedicatedRequest)
{
    auto request = requestFor(1024);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    request.dedicated.requiresDedicatedAllocation = vk::True;
    const auto range = MemoryRange {.offset = 0, .size = 1024};
    EXPECT_CALL(blocks, createDedicated(_, _, testing::Field(&vk::MemoryAllocateInfo::allocationSize, 1024), _, _))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(blocks, allocate(_, 1024, 16)).WillOnce(Return(range));
    EXPECT_CALL(blocks, getFreeSize(_)).WillOnce(Return(1024));
    EXPECT_CALL(blocks, getCapacity(_)).WillRepeatedly(Return(1024));
    EXPECT_CALL(blocks, release(_, range)).WillOnce(Return(panda::Result<void> {}));

    auto allocation = pool->allocate(request);

    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange(), range);
    allocation->reset();
    EXPECT_EQ(pool->getStats().allocationCount, 0);
    EXPECT_EQ(pool->getStats().committedBytes, 0);
}

TEST_F(MemoryPoolTest, ExportsOnlyDedicatedAllocations)
{
    auto request = requestFor();
    constexpr auto handleTypes = vk::ExternalMemoryHandleTypeFlags {vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd};
    request.exportHandleTypes = handleTypes;
    const auto range = MemoryRange {.offset = 0, .size = 64};
    EXPECT_CALL(blocks,
                createDedicated(_, _, testing::Field(&vk::MemoryAllocateInfo::allocationSize, 64), _, handleTypes))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(blocks, allocate(_, 64, 16)).WillOnce(Return(range));
    EXPECT_CALL(blocks, getFreeSize(_)).WillOnce(Return(64));
    EXPECT_CALL(blocks, getCapacity(_)).WillRepeatedly(Return(64));
    EXPECT_CALL(blocks, release(_, range)).WillOnce(Return(panda::Result<void> {}));

    auto allocation = pool->allocate(request);

    ASSERT_TRUE(allocation);
    allocation->reset();
    EXPECT_EQ(pool->getStats().allocationCount, 0);
}

TEST_F(MemoryPoolTest, RetriesRecoverableOrdinaryBlockFailureWithOneDedicatedAttempt)
{
    const auto range = MemoryRange {.offset = 0, .size = 64};
    EXPECT_CALL(blocks, create(_, _, 64 * mebibyte, 0))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled block failure")}}));
    EXPECT_CALL(blocks, createDedicated(_, _, _, _, _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(blocks, allocate(_, 64, 16)).WillOnce(Return(range));
    EXPECT_CALL(blocks, getFreeSize(_)).WillOnce(Return(64));
    EXPECT_CALL(blocks, getCapacity(_)).WillRepeatedly(Return(64));
    EXPECT_CALL(blocks, release(_, range)).WillOnce(Return(panda::Result<void> {}));

    auto allocation = pool->allocate(requestFor());

    ASSERT_TRUE(allocation);
    allocation->reset();
    EXPECT_EQ(pool->getStats().allocationCount, 0);
}

TEST_F(MemoryPoolTest, DoesNotRetryDeviceLossOrExceedTheAllocationCount)
{
    EXPECT_CALL(blocks, create(_, _, 64 * mebibyte, 0))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::DeviceLost, "Controlled device loss")}}));
    const auto lost = pool->allocate(requestFor());
    ASSERT_FALSE(lost);
    EXPECT_EQ(lost.error().code, panda::ErrorCode::DeviceLost);

    EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
        .WillOnce([this](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
            *output = memoryProperties;
        });
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
        .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
            output->properties.limits.maxMemoryAllocationCount = 0;
            output->properties.limits.nonCoherentAtomSize = 64;
            auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
            ASSERT_NE(maintenance, nullptr);
            maintenance->maxMemoryAllocationSize = 128 * mebibyte;
        });
    auto noBudgetPool = MemoryPool::create(physical, device);
    const auto exhausted = noBudgetPool->allocate(requestFor());
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, panda::ErrorCode::ResourceExhausted);
}

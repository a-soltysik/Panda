// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "resources/memory/MemoryBlock.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <panda/Error.hpp>
#include <span>
#include <tuple>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/RangeAllocatorMock.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryBlock;
using panda::core::vulkan::resources::MemoryRange;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);

class MemoryBlockTest : public testing::Test
{
protected:
    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectDeviceProperties(vk::MemoryPropertyFlags flags, std::uint64_t size, std::uint64_t atomSize = 64)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([flags, size](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {.propertyFlags = static_cast<VkMemoryPropertyFlags>(flags), .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = size, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([atomSize, size](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.nonCoherentAtomSize = atomSize;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = size;
            });
    }

    void expectAllocation(std::uint64_t size)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
            .WillOnce([this, size](VkDevice,
                                   const VkMemoryAllocateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkDeviceMemory* output) {
                EXPECT_EQ(info->allocationSize, size);
                EXPECT_EQ(info->memoryTypeIndex, 0);
                *output = memory;
                return success;
            });
    }

    void expectCleanAllocator(std::uint64_t size)
    {
        EXPECT_CALL(allocator, getFreeSize()).WillRepeatedly(Return(size));
        EXPECT_CALL(allocator, getCapacity()).WillRepeatedly(Return(size));
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::RangeAllocatorMock> allocator;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    VkDeviceMemory memory {fakeVulkanHandle<VkDeviceMemory>(4)};
};
}

TEST_F(MemoryBlockTest, DelegatesAllocationAndReleaseToItsRangeAllocator)
{
    constexpr auto capacity = std::uint64_t {128};
    expectDeviceProperties(vk::MemoryPropertyFlagBits::eDeviceLocal, capacity);
    expectAllocation(capacity);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    EXPECT_CALL(allocator, construct(capacity));
    expectCleanAllocator(capacity);
    EXPECT_CALL(allocator, allocate(8, 16)).WillOnce(Return(MemoryRange {.offset = 16, .size = 8}));
    EXPECT_CALL(allocator, release(MemoryRange {.offset = 16, .size = 8})).WillOnce(Return(panda::Result<void> {}));

    auto created = MemoryBlock::create(physical, device, capacity, 0);

    ASSERT_TRUE(created);
    auto& block = **created;
    const auto range = block.allocate(8, 16);
    ASSERT_TRUE(range);
    EXPECT_EQ(*range, (MemoryRange {.offset = 16, .size = 8}));
    EXPECT_EQ(block.getFreeSize(), capacity);
    EXPECT_EQ(block.getCapacity(), capacity);
    EXPECT_EQ(block.getMemory(), vk::DeviceMemory {memory});
    EXPECT_TRUE(block.release(*range));
}

TEST_F(MemoryBlockTest, RejectsInvalidRangesBeforeCallingTheAllocator)
{
    constexpr auto capacity = std::uint64_t {128};
    expectDeviceProperties(vk::MemoryPropertyFlagBits::eDeviceLocal, capacity);
    expectAllocation(capacity);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    EXPECT_CALL(allocator, construct(capacity));
    expectCleanAllocator(capacity);

    auto created = MemoryBlock::create(physical, device, capacity, 0);

    ASSERT_TRUE(created);
    EXPECT_EQ(created.value()->allocate(0, 1).error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(created.value()->allocate(1, 0).error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(created.value()->allocate(1, 3).error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MemoryBlockTest, PreservesRangeAllocatorFailure)
{
    constexpr auto capacity = std::uint64_t {128};
    expectDeviceProperties(vk::MemoryPropertyFlagBits::eDeviceLocal, capacity);
    expectAllocation(capacity);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    EXPECT_CALL(allocator, construct(capacity));
    expectCleanAllocator(capacity);
    EXPECT_CALL(allocator, allocate(32, 8))
        .WillOnce(Return(panda::Result<MemoryRange> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled range failure")}}));

    auto created = MemoryBlock::create(physical, device, capacity, 0);

    ASSERT_TRUE(created);
    const auto allocation = created.value()->allocate(32, 8);
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MemoryBlockTest, MapsLiveRangesAndRoundsCacheOperationsWithinTheRange)
{
    constexpr auto capacity = std::uint64_t {256};
    std::array<std::byte, 256> storage {};
    expectDeviceProperties(vk::MemoryPropertyFlagBits::eHostVisible, capacity);
    expectAllocation(capacity);
    EXPECT_CALL(vulkan, vkMapMemory(_, memory, 0, capacity, 0, NotNull()))
        .WillOnce([&storage](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** output) {
            *output = storage.data();
            return success;
        });
    const auto unmapped = testing::Expectation {EXPECT_CALL(vulkan, vkUnmapMemory(_, memory))};
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr)).After(unmapped);
    EXPECT_CALL(allocator, construct(capacity));
    EXPECT_CALL(allocator, getCapacity()).WillRepeatedly(Return(capacity));
    const auto range = MemoryRange {.offset = 64, .size = 64};
    EXPECT_CALL(allocator, isLive(range.offset, range.size)).Times(3).WillRepeatedly(Return(true));
    EXPECT_CALL(allocator, getFreeSize()).WillRepeatedly(Return(capacity));
    EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull()))
        .WillOnce([this](VkDevice, std::uint32_t count, const VkMappedMemoryRange* mapped) {
            EXPECT_EQ(count, 1);
            EXPECT_EQ(mapped->memory, memory);
            EXPECT_EQ(mapped->offset, 64);
            EXPECT_EQ(mapped->size, 64);
            return success;
        });
    EXPECT_CALL(vulkan, vkInvalidateMappedMemoryRanges(_, 1, NotNull()))
        .WillOnce([this](VkDevice, std::uint32_t count, const VkMappedMemoryRange* mapped) {
            EXPECT_EQ(count, 1);
            EXPECT_EQ(mapped->memory, memory);
            EXPECT_EQ(mapped->offset, 64);
            EXPECT_EQ(mapped->size, 64);
            return success;
        });

    auto created = MemoryBlock::create(physical, device, capacity, 0);

    ASSERT_TRUE(created);
    auto& block = **created;
    const auto mapped = block.getMappedBytes(range);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(mapped->data(), std::span {storage}.subspan(range.offset).data());
    EXPECT_TRUE(block.flush(range, 3, 5));
    EXPECT_TRUE(block.invalidate(range, 3, 5));
}

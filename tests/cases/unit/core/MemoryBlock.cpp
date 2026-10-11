// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "MemoryBlock.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <panda/Error.hpp>
#include <tuple>

#include "RangeAllocator.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"

namespace
{
using panda::detail::MemoryBlock;
using panda::detail::MemoryRange;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

class MemoryBlockTest : public testing::Test
{
protected:
    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectMemoryTypes(vk::MemoryPropertyFlags flags)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([flags](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                *output = VkPhysicalDeviceMemoryProperties {};
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {.propertyFlags = static_cast<VkMemoryPropertyFlags>(flags), .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = 1024, .flags = 0};
            });
    }

    void expectLimits(std::uint64_t atomSize = 64, std::uint64_t maximum = 1024)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([atomSize, maximum](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.nonCoherentAtomSize = atomSize;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                EXPECT_EQ(maintenance->sType,
                          static_cast<VkStructureType>(vk::StructureType::ePhysicalDeviceMaintenance3Properties));
                maintenance->maxMemoryAllocationSize = maximum;
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
                EXPECT_EQ(info->pNext, nullptr);
                *output = memory;
                return success;
            });
    }

    void expectMapping(std::uint64_t size)
    {
        EXPECT_CALL(vulkan, vkMapMemory(_, memory, 0, size, 0, NotNull()))
            .WillOnce([this](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** output) {
                *output = storage.data();
                return success;
            });
        const auto unmapped = testing::Expectation {EXPECT_CALL(vulkan, vkUnmapMemory(_, memory))};
        EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr)).After(unmapped);
    }

    void expectFlushed(std::uint64_t offset, std::uint64_t size)
    {
        EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull()))
            .WillOnce([this, offset, size](VkDevice, std::uint32_t, const VkMappedMemoryRange* range) {
                EXPECT_EQ(range->memory, memory);
                EXPECT_EQ(range->offset, offset);
                EXPECT_EQ(range->size, size);
                return success;
            });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    VkDeviceMemory memory {fakeVulkanHandle<VkDeviceMemory>(4)};
    std::array<std::byte, 1024> storage {};
};
}

TEST_F(MemoryBlockTest, DeviceLocalBlockAllocatesOnceAndReusesReleasedRanges)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    expectLimits();
    expectAllocation(128);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    EXPECT_EQ(block.getMemory(), vk::DeviceMemory {memory});
    const auto first = block.allocate(3, 1);
    const auto aligned = block.allocate(8, 16);
    ASSERT_TRUE(first);
    ASSERT_TRUE(aligned);
    EXPECT_EQ(aligned->offset, 16);
    EXPECT_EQ(block.getFreeSize(), 117);
    EXPECT_FALSE(block.getMappedBytes(*first));
    ASSERT_TRUE(block.release(*first));
    ASSERT_TRUE(block.release(*aligned));
    EXPECT_EQ(block.getFreeSize(), 128);
}

TEST_F(MemoryBlockTest, CoherentMemoryMapsOnceAndNeedsNoCacheCalls)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    expectLimits();
    expectAllocation(128);
    expectMapping(128);
    auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    const auto slice = block.allocate(7, 1);
    ASSERT_TRUE(slice);
    EXPECT_EQ(slice->size, 7);
    const auto bytes = block.getMappedBytes(*slice);
    ASSERT_TRUE(bytes);
    EXPECT_EQ(bytes->data(), storage.data());
    ASSERT_TRUE(block.flush(*slice, 1, 3));
    ASSERT_TRUE(block.invalidate(*slice, 1, 3));
    ASSERT_TRUE(block.release(*slice));
}

TEST_F(MemoryBlockTest, NoncoherentSlicesAndCacheOperationsStayOnSeparateAtoms)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible);
    expectLimits();
    expectAllocation(192);
    expectMapping(192);
    auto created = MemoryBlock::create(physical, device, 192, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    const auto first = block.allocate(1, 1);
    const auto second = block.allocate(65, 1);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, (MemoryRange {.offset = 0, .size = 64}));
    EXPECT_EQ(*second, (MemoryRange {.offset = 64, .size = 128}));
    expectFlushed(64, 128);
    ASSERT_TRUE(block.flush(*second, 3, 64));
    ASSERT_TRUE(block.release(*first));
    ASSERT_TRUE(block.release(*second));
}

TEST_F(MemoryBlockTest, FailedAllocationDoesNotFreeAnUndefinedOutput)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    expectLimits();
    EXPECT_CALL(vulkan, vkAllocateMemory(_, _, _, NotNull()))
        .WillOnce([this](VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* output) {
            *output = memory;
            return exhausted;
        });
    const auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MemoryBlockTest, FailedMappingFreesMemoryWithoutUnmappingUndefinedOutput)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible);
    expectLimits();
    expectAllocation(128);
    EXPECT_CALL(vulkan, vkMapMemory(_, memory, _, _, _, NotNull()))
        .WillOnce([](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**) {
            return static_cast<VkResult>(vk::Result::eErrorMemoryMapFailed);
        });
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    const auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(MemoryBlockTest, RejectsInvalidMemoryTypeBeforeCallingAllocate)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    const auto created = MemoryBlock::create(physical, device, 128, 1);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MemoryBlockTest, RejectsAllocationLargerThanDeviceLimit)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    expectLimits(64, 64);
    const auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MemoryBlockTest, RejectsAllocationLargerThanHeap)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    expectLimits(64, 4096);
    const auto created = MemoryBlock::create(physical, device, 2048, 0);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MemoryBlockTest, InvalidAndOverflowingSliceRequestsPreserveBlock)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible);
    expectLimits();
    expectAllocation(128);
    expectMapping(128);
    auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    EXPECT_FALSE(block.allocate(0, 1));
    EXPECT_FALSE(block.allocate(1, 3));
    EXPECT_FALSE(block.allocate(std::numeric_limits<std::uint64_t>::max(), 1));
    EXPECT_EQ(block.getFreeSize(), 128);
    const auto slice = block.allocate(1, 1);
    ASSERT_TRUE(slice);
    EXPECT_FALSE(block.flush(*slice, 0, 0));
    EXPECT_FALSE(block.flush(*slice, 63, 2));
    EXPECT_FALSE(block.invalidate(*slice, std::numeric_limits<std::uint64_t>::max(), 1));
    EXPECT_FALSE(block.release(MemoryRange {.offset = slice->offset, .size = 1}));
    ASSERT_TRUE(block.release(*slice));
    EXPECT_FALSE(block.getMappedBytes(*slice));
    EXPECT_FALSE(block.flush(*slice, 0, 1));
    EXPECT_EQ(block.getFreeSize(), 128);
}

TEST_F(MemoryBlockTest, NoncoherentAtomSizeOneStillFlushesAndPropagatesFailure)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible);
    expectLimits(1);
    expectAllocation(128);
    expectMapping(128);
    auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    const auto slice = block.allocate(7, 1);
    ASSERT_TRUE(slice);
    EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull())).WillOnce(Return(exhausted));
    const auto flushed = block.flush(*slice, 0, 7);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_CALL(vulkan, vkInvalidateMappedMemoryRanges(_, 1, NotNull())).WillOnce(Return(success));
    ASSERT_TRUE(block.invalidate(*slice, 0, 7));
    ASSERT_TRUE(block.release(*slice));
}

TEST_F(MemoryBlockTest, RejectsEmptyBlockBeforeAllocation)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    const auto created = MemoryBlock::create(physical, device, 0, 0);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MemoryBlockTest, RejectsProtectedAndLazyMemoryForOrdinaryBlocks)
{
    const auto flags =
        std::array {vk::MemoryPropertyFlagBits::eProtected, vk::MemoryPropertyFlagBits::eLazilyAllocated};
    for (const auto flag : flags)
    {
        expectMemoryTypes(flag);
        expectLimits();
        const auto created = MemoryBlock::create(physical, device, 128, 0);
        ASSERT_FALSE(created);
        EXPECT_EQ(created.error().code, panda::ErrorCode::Unsupported);
    }
}

TEST_F(MemoryBlockTest, NoncoherentRoundingDoesNotEscapeMappedBlockTail)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eHostVisible);
    expectLimits();
    expectAllocation(130);
    expectMapping(130);
    auto created = MemoryBlock::create(physical, device, 130, 0);
    ASSERT_TRUE(created);
    auto& block = **created;
    const auto oversized = block.allocate(129, 1);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(block.getFreeSize(), 130);
    const auto slice = block.allocate(65, 128);
    ASSERT_TRUE(slice);
    expectFlushed(64, 64);
    ASSERT_TRUE(block.flush(*slice, 127, 1));
    EXPECT_CALL(vulkan, vkInvalidateMappedMemoryRanges(_, 1, NotNull())).WillOnce(Return(exhausted));
    const auto invalidated = block.invalidate(*slice, 0, 1);
    ASSERT_FALSE(invalidated);
    EXPECT_EQ(invalidated.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(block.release(*slice));
}

using MemoryBlockDeathTest = MemoryBlockTest;

TEST_F(MemoryBlockDeathTest, RejectsDestructionWithLiveAllocations)
{
    expectMemoryTypes(vk::MemoryPropertyFlagBits::eDeviceLocal);
    expectLimits();
    expectAllocation(128);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto created = MemoryBlock::create(physical, device, 128, 0);
    ASSERT_TRUE(created);
    const auto range = (*created)->allocate(1, 1);
    ASSERT_TRUE(range);
    EXPECT_DEATH(created->reset(), "Memory block destroyed with live allocations");
    ASSERT_TRUE((*created)->release(*range));
}

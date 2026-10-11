// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "MemoryPool.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <panda/Error.hpp>
#include <tuple>
#include <utility>
#include <vector>

#include "MemoryAllocation.hpp"
#include "RangeAllocator.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"

namespace
{
using panda::detail::MemoryPool;
using panda::detail::MemoryRequest;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;
constexpr auto mebibyte = std::uint64_t {1024} * 1024;
constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto bufferRequest(std::uint64_t size = 64) -> MemoryRequest
{
    return MemoryRequest {
        .requirements = {.size = size, .alignment = 16, .memoryTypeBits = 1},
        .resource = {.buffer = vk::Buffer {fakeVulkanHandle<VkBuffer>(20)}},
        .requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
        .preferredProperties = {}
    };
}

void verifyAllocateInfo(const VkMemoryAllocateInfo& info, std::uint64_t size, std::uint32_t type, bool dedicated)
{
    EXPECT_EQ(info.allocationSize, size);
    EXPECT_EQ(info.memoryTypeIndex, type);
    if (!dedicated)
    {
        EXPECT_EQ(info.pNext, nullptr);
        return;
    }
    const auto* resource = static_cast<const VkMemoryDedicatedAllocateInfo*>(info.pNext);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->buffer, fakeVulkanHandle<VkBuffer>(20));
    EXPECT_EQ(resource->image, VkImage {});
}

class MemoryPoolTest : public testing::Test
{
protected:
    void SetUp() override
    {
        configureMemoryProperties();
        expectMemoryProperties();
        expectLimits();
    }

    void configureMemoryProperties()
    {
        memoryProperties.memoryTypeCount = 3;
        memoryProperties.memoryTypes[0] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
            .heapIndex = 0};
        memoryProperties.memoryTypes[1] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostVisible),
            .heapIndex = 0};
        memoryProperties.memoryTypes[2] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostVisible |
                                                                vk::MemoryPropertyFlagBits::eHostCoherent),
            .heapIndex = 0};
        memoryProperties.memoryHeapCount = 1;
    }

    void expectMemoryProperties()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                *output = memoryProperties;
                output->memoryHeaps[0] = {.size = heapSize, .flags = 0};
            });
    }

    void expectLimits()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.maxMemoryAllocationCount = maximumCount;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = maximumSize;
            });
    }

    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void configureTwoHeaps()
    {
        heapSize = 1024;
        memoryProperties.memoryHeapCount = 2;
        memoryProperties.memoryHeaps[1] = {.size = 2048, .flags = 0};
        memoryProperties.memoryTypes[1] = {
            .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
            .heapIndex = 1};
    }

    void expectAllocation(std::uint64_t size, std::uint32_t type, VkDeviceMemory memory, bool dedicated = false)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
            .InSequence(allocationOrder)
            .WillOnce([size, type, memory, dedicated](VkDevice,
                                                      const VkMemoryAllocateInfo* info,
                                                      const VkAllocationCallbacks*,
                                                      VkDeviceMemory* output) {
                verifyAllocateInfo(*info, size, type, dedicated);
                *output = memory;
                return success;
            });
    }

    auto expectMapped(VkDeviceMemory memory, std::uint64_t size, std::size_t slot = 0) -> testing::Expectation
    {
        auto& bytes = storage.at(slot);
        bytes.resize(std::size_t {size});
        EXPECT_CALL(vulkan, vkMapMemory(_, memory, 0, size, 0, NotNull()))
            .WillOnce([&bytes](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** output) {
                *output = bytes.data();
                return success;
            });
        const auto unmapped = testing::Expectation {EXPECT_CALL(vulkan, vkUnmapMemory(_, memory))};
        return testing::Expectation {EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr)).After(unmapped)};
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::uint64_t heapSize {256 * mebibyte};
    std::uint64_t maximumSize {256 * mebibyte};
    std::uint32_t maximumCount {16};
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    std::array<std::vector<std::byte>, 2> storage;
    testing::Sequence allocationOrder;
};
}

TEST_F(MemoryPoolTest, CreatesLazilyAndReusesRangesAfterCoalescing)
{
    auto pool = MemoryPool::create(physical, device);
    EXPECT_EQ(pool->getStats().allocationCount, 0);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto first = pool->allocate(bufferRequest());
    auto second = pool->allocate(bufferRequest());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->getMemory(), second->getMemory());
    EXPECT_EQ(second->getRange().offset, 64);
    first->reset();
    second->reset();
    auto merged = pool->allocate(bufferRequest(128));
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->getRange().offset, 0);
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().liveBytes, 128);
}

TEST_F(MemoryPoolTest, PrefersCoherentHostMemoryAndUsesEightMebibyteBlocks)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 2, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 6;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange().size, 65);
    EXPECT_EQ(pool->getStats().committedBytes, 8 * mebibyte);
    ASSERT_TRUE(allocation->flush(0, 65));
}

TEST_F(MemoryPoolTest, NoncoherentPaddingCountsAsReservedLiveBytes)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 1, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto first = pool->allocate(request);
    auto second = pool->allocate(request);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->getRange().size, 128);
    EXPECT_EQ(second->getRange().offset, 128);
    EXPECT_EQ(pool->getStats().liveBytes, 256);
    EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull())).WillOnce(Return(success));
    ASSERT_TRUE(first->flush(1, 64));
}

TEST_F(MemoryPoolTest, SeparatesBuffersAndOptimalImages)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const bufferMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const imageMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(64 * mebibyte, 0, bufferMemory);
    expectAllocation(64 * mebibyte, 0, imageMemory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, bufferMemory, nullptr));
    EXPECT_CALL(vulkan, vkFreeMemory(_, imageMemory, nullptr));
    auto buffer = pool->allocate(bufferRequest());
    auto request = bufferRequest();
    request.resource = vk::MemoryDedicatedAllocateInfo {.image = vk::Image {fakeVulkanHandle<VkImage>(21)}};
    auto image = pool->allocate(request);
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(image);
    EXPECT_NE(buffer->getMemory(), image->getMemory());
    EXPECT_EQ(pool->getStats().allocationCount, 2);
}

TEST_F(MemoryPoolTest, HonorsRequiredAndPreferredDedicatedAllocations)
{
    auto pool = MemoryPool::create(physical, device);
    for (const auto required : {false, true})
    {
        auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
        expectAllocation(64, 0, memory, true);
        EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
        auto request = bufferRequest();
        request.dedicated.requiresDedicatedAllocation = required ? vk::True : vk::False;
        request.dedicated.prefersDedicatedAllocation = required ? vk::False : vk::True;
        auto allocation = pool->allocate(request);
        ASSERT_TRUE(allocation);
        EXPECT_EQ(allocation->getRange().offset, 0);
        EXPECT_EQ(pool->getStats().committedBytes, 64);
        allocation->reset();
        EXPECT_EQ(pool->getStats().allocationCount, 0);
    }
}

TEST_F(MemoryPoolTest, RequestsAboveHalfBlockUseExactDedicatedMemory)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(33 * mebibyte, 0, memory, true);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto allocation = pool->allocate(bufferRequest(33 * mebibyte));
    ASSERT_TRUE(allocation);
    EXPECT_EQ(pool->getStats().committedBytes, 33 * mebibyte);
}

TEST_F(MemoryPoolTest, KeepsAtMostOneEmptyOrdinaryBlockPerPool)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const secondMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(64 * mebibyte, 0, firstMemory);
    expectAllocation(64 * mebibyte, 0, secondMemory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, firstMemory, nullptr));
    EXPECT_CALL(vulkan, vkFreeMemory(_, secondMemory, nullptr));
    auto first = pool->allocate(bufferRequest(32 * mebibyte));
    auto second = pool->allocate(bufferRequest(32 * mebibyte));
    auto third = pool->allocate(bufferRequest(32 * mebibyte));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    EXPECT_EQ(pool->getStats().allocationCount, 2);
    first->reset();
    third->reset();
    second->reset();
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().committedBytes, 64 * mebibyte);
    EXPECT_EQ(pool->getStats().liveBytes, 0);
}

TEST_F(MemoryPoolTest, FailedGrowthFallsBackOnceWithoutChangingExistingAllocations)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const original = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const fallback = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(64 * mebibyte, 0, original);
    EXPECT_CALL(vulkan, vkFreeMemory(_, original, nullptr));
    EXPECT_CALL(vulkan, vkFreeMemory(_, fallback, nullptr));
    auto first = pool->allocate(bufferRequest(32 * mebibyte));
    auto second = pool->allocate(bufferRequest(32 * mebibyte));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
        .InSequence(allocationOrder)
        .WillOnce(Return(exhausted));
    expectAllocation(64, 0, fallback, true);
    auto third = pool->allocate(bufferRequest());
    ASSERT_TRUE(third);
    EXPECT_EQ(first->getMemory(), vk::DeviceMemory {original});
    EXPECT_EQ(second->getRange().offset, 32 * mebibyte);
    EXPECT_EQ(third->getMemory(), vk::DeviceMemory {fallback});
    EXPECT_EQ(pool->getStats().committedBytes, (64 * mebibyte) + 64);
}

TEST_F(MemoryPoolTest, FailedOrdinaryAndDedicatedAttemptsPreserveLiveState)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const original = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, original);
    EXPECT_CALL(vulkan, vkFreeMemory(_, original, nullptr));
    auto first = pool->allocate(bufferRequest(32 * mebibyte));
    auto second = pool->allocate(bufferRequest(32 * mebibyte));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
        .Times(2)
        .InSequence(allocationOrder)
        .WillRepeatedly(Return(exhausted));
    const auto failed = pool->allocate(bufferRequest());
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().liveBytes, 64 * mebibyte);
    EXPECT_EQ(first->getMemory(), vk::DeviceMemory {original});
}

TEST_F(MemoryPoolTest, DeviceLossDoesNotTriggerAnAllocationRetry)
{
    auto pool = MemoryPool::create(physical, device);
    EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));
    const auto allocation = pool->allocate(bufferRequest());
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_EQ(pool->getStats().committedBytes, 0);
}

TEST_F(MemoryPoolTest, ChecksAllocationCountBeforeCallingVulkan)
{
    maximumCount = 1;
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto buffer = pool->allocate(bufferRequest());
    ASSERT_TRUE(buffer);
    auto request = bufferRequest();
    request.resource = vk::MemoryDedicatedAllocateInfo {.image = vk::Image {fakeVulkanHandle<VkImage>(21)}};
    const auto image = pool->allocate(request);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MemoryPoolTest, EmptyCacheCanBeReclaimedToRespectAllocationCount)
{
    maximumCount = 1;
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const secondMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(64 * mebibyte, 0, firstMemory);
    auto first = pool->allocate(bufferRequest());
    ASSERT_TRUE(first);
    first->reset();
    const auto freed = testing::Expectation {EXPECT_CALL(vulkan, vkFreeMemory(_, firstMemory, nullptr))};
    EXPECT_CALL(vulkan, vkAllocateMemory(_, _, _, NotNull()))
        .InSequence(allocationOrder)
        .After(freed)
        .WillOnce([secondMemory](VkDevice,
                                 const VkMemoryAllocateInfo*,
                                 const VkAllocationCallbacks*,
                                 VkDeviceMemory* output) {
            *output = secondMemory;
            return success;
        });
    EXPECT_CALL(vulkan, vkFreeMemory(_, secondMemory, nullptr));
    auto request = bufferRequest();
    request.resource = vk::MemoryDedicatedAllocateInfo {.image = vk::Image {fakeVulkanHandle<VkImage>(21)}};
    auto second = pool->allocate(request);
    ASSERT_TRUE(second);
    EXPECT_EQ(pool->getStats().allocationCount, 1);
}

TEST_F(MemoryPoolTest, CapsBlockAtHeapAndRejectsGrowthBeyondCommittedBudget)
{
    heapSize = 1024;
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(1024, 0, firstMemory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, firstMemory, nullptr));
    auto first = pool->allocate(bufferRequest(512));
    auto second = pool->allocate(bufferRequest(512));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto exhaustedHeap = pool->allocate(bufferRequest(1));
    ASSERT_FALSE(exhaustedHeap);
    EXPECT_EQ(exhaustedHeap.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(pool->getStats().committedBytes, 1024);
}

TEST_F(MemoryPoolTest, MovingKeepsTheRangeOwnedUntilReset)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto created = pool->allocate(bufferRequest());
    ASSERT_TRUE(created);
    auto moved = std::move(*created);
    EXPECT_FALSE(created->isValid());
    EXPECT_FALSE(created->getMappedBytes());
    EXPECT_EQ(pool->getStats().liveBytes, 64);
    auto other = pool->allocate(bufferRequest());
    ASSERT_TRUE(other);
    EXPECT_EQ(other->getRange().offset, 64);
    moved.reset();
    EXPECT_EQ(pool->getStats().liveBytes, 64);
    auto reused = pool->allocate(bufferRequest());
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->getRange().offset, 0);
    *other = std::move(moved);
    EXPECT_FALSE(other->isValid());
    EXPECT_EQ(pool->getStats().liveBytes, 64);
}

TEST_F(MemoryPoolTest, InvalidAndUnsupportedRequestsDoNotAllocate)
{
    auto pool = MemoryPool::create(physical, device);
    auto request = bufferRequest(0);
    EXPECT_FALSE(pool->allocate(request));
    request = bufferRequest();
    request.requirements.alignment = 3;
    EXPECT_FALSE(pool->allocate(request));
    request = bufferRequest();
    request.resource = vk::MemoryDedicatedAllocateInfo {};
    EXPECT_FALSE(pool->allocate(request));
    request = bufferRequest();
    request.resource.image = vk::Image {fakeVulkanHandle<VkImage>(21)};
    EXPECT_FALSE(pool->allocate(request));
    request = bufferRequest();
    request.requirements.memoryTypeBits = 0;
    const auto unsupported = pool->allocate(request);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(pool->getStats().allocationCount, 0);
}

TEST_F(MemoryPoolTest, DedicatedNoncoherentMappingFlushesTheExactAllocationTail)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(65, 1, memory, true);
    expectMapped(memory, 65);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    request.dedicated.requiresDedicatedAllocation = vk::True;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange().size, 65);
    EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull()))
        .WillOnce([](VkDevice, std::uint32_t, const VkMappedMemoryRange* range) {
            EXPECT_EQ(range->offset, 64);
            EXPECT_EQ(range->size, 1);
            return success;
        });
    ASSERT_TRUE(allocation->flush(64, 1));
    allocation->reset();
    EXPECT_EQ(pool->getStats().committedBytes, 0);
}

TEST_F(MemoryPoolTest, MappingFailureFreesTheAttemptAndDoesNotPublishPoolState)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 2, memory);
    EXPECT_CALL(vulkan, vkMapMemory(_, memory, 0, 8 * mebibyte, 0, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorMemoryMapFailed)));
    expectAllocation(64, 2, memory, true);
    EXPECT_CALL(vulkan, vkMapMemory(_, memory, 0, 64, 0, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorMemoryMapFailed)));
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr)).Times(2);
    auto request = bufferRequest();
    request.requirements.memoryTypeBits = 4;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    const auto failed = pool->allocate(request);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::BackendFailure);
    EXPECT_EQ(pool->getStats().allocationCount, 0);
    EXPECT_EQ(pool->getStats().liveBytes, 0);
    EXPECT_EQ(pool->getStats().committedBytes, 0);
}

TEST_F(MemoryPoolTest, RemainingHeapBudgetCanFitOneExactDedicatedFallback)
{
    heapSize = 100 * mebibyte;
    auto pool = MemoryPool::create(physical, device);
    auto* const original = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const fallback = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(64 * mebibyte, 0, original);
    EXPECT_CALL(vulkan, vkFreeMemory(_, original, nullptr));
    auto first = pool->allocate(bufferRequest(32 * mebibyte));
    auto second = pool->allocate(bufferRequest(32 * mebibyte));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    expectAllocation(24 * mebibyte, 0, fallback, true);
    EXPECT_CALL(vulkan, vkFreeMemory(_, fallback, nullptr));
    auto third = pool->allocate(bufferRequest(24 * mebibyte));
    ASSERT_TRUE(third);
    EXPECT_EQ(pool->getStats().committedBytes, 88 * mebibyte);
    EXPECT_EQ(pool->getStats().allocationCount, 2);
}

TEST_F(MemoryPoolTest, FailedLargeMappingCanUseOneSmallerDedicatedMapping)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const original = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const fallback = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(8 * mebibyte, 2, original);
    EXPECT_CALL(vulkan, vkMapMemory(_, original, 0, 8 * mebibyte, 0, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorMemoryMapFailed)));
    EXPECT_CALL(vulkan, vkFreeMemory(_, original, nullptr));
    expectAllocation(64, 2, fallback, true);
    expectMapped(fallback, 64);
    auto request = bufferRequest();
    request.requirements.memoryTypeBits = 4;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getMemory(), vk::DeviceMemory {fallback});
    EXPECT_EQ(pool->getStats().committedBytes, 64);
    EXPECT_EQ(pool->getStats().allocationCount, 1);
}

TEST_F(MemoryPoolTest, MoveAssignmentReleasesThePreviousRangeAndTransfersTheNewOne)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto first = pool->allocate(bufferRequest());
    auto second = pool->allocate(bufferRequest());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    *first = std::move(*second);
    EXPECT_FALSE(second->isValid());
    EXPECT_EQ(first->getRange().offset, 64);
    EXPECT_EQ(pool->getStats().liveBytes, 64);
    auto reused = pool->allocate(bufferRequest());
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->getRange().offset, 0);
}

TEST_F(MemoryPoolTest, MaximumSingleAllocationSizeCapsOrdinaryBlocks)
{
    maximumSize = 1024;
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(1024, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto allocation = pool->allocate(bufferRequest());
    ASSERT_TRUE(allocation);
    const auto tooLarge = pool->allocate(bufferRequest(2048));
    ASSERT_FALSE(tooLarge);
    EXPECT_EQ(tooLarge.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(pool->getStats().committedBytes, 1024);
}

TEST_F(MemoryPoolTest, DifferentMemoryTypesNeverShareOneBlock)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const secondMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(8 * mebibyte, 1, firstMemory);
    expectMapped(firstMemory, 8 * mebibyte, 0);
    expectAllocation(8 * mebibyte, 2, secondMemory);
    expectMapped(secondMemory, 8 * mebibyte, 1);
    auto request = bufferRequest();
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    request.requirements.memoryTypeBits = 2;
    auto first = pool->allocate(request);
    request.requirements.memoryTypeBits = 4;
    auto second = pool->allocate(request);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(first->getMemory(), second->getMemory());
    EXPECT_EQ(pool->getStats().allocationCount, 2);
}

TEST_F(MemoryPoolTest, DedicatedImageAllocationNamesOnlyItsImage)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
        .InSequence(allocationOrder)
        .WillOnce(
            [memory](VkDevice, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* output) {
                EXPECT_EQ(info->allocationSize, 64);
                const auto* dedicated = static_cast<const VkMemoryDedicatedAllocateInfo*>(info->pNext);
                EXPECT_NE(dedicated, nullptr);
                if (dedicated != nullptr)
                {
                    EXPECT_EQ(dedicated->image, fakeVulkanHandle<VkImage>(21));
                    EXPECT_EQ(dedicated->buffer, VkBuffer {});
                }
                *output = memory;
                return success;
            });
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto request = bufferRequest();
    request.resource = vk::MemoryDedicatedAllocateInfo {.image = vk::Image {fakeVulkanHandle<VkImage>(21)}};
    request.dedicated.prefersDedicatedAllocation = vk::True;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange().offset, 0);
}

TEST_F(MemoryPoolTest, EmptyOwnerRejectsMappedOperationsAndResetIsIdempotent)
{
    auto allocation = panda::detail::MemoryAllocation {};
    EXPECT_FALSE(allocation.isValid());
    const auto bytes = allocation.getMappedBytes();
    const auto flushed = allocation.flush(0, 1);
    const auto invalidated = allocation.invalidate(0, 1);
    ASSERT_FALSE(bytes);
    ASSERT_FALSE(flushed);
    ASSERT_FALSE(invalidated);
    EXPECT_EQ(bytes.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(flushed.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(invalidated.error().code, panda::ErrorCode::InvalidArgument);
    allocation.reset();
    allocation.reset();
}

TEST_F(MemoryPoolTest, DeviceOnlyOwnerRejectsHostCacheOperations)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto allocation = pool->allocate(bufferRequest());
    ASSERT_TRUE(allocation);
    const auto flushed = allocation->flush(0, 1);
    const auto invalidated = allocation->invalidate(0, 1);
    ASSERT_FALSE(flushed);
    ASSERT_FALSE(invalidated);
    EXPECT_EQ(flushed.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(invalidated.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(MemoryPoolTest, WrapperRoundsNoncoherentFlushAndPreservesNativeErrors)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 1, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto prefix = pool->allocate(request);
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(prefix);
    ASSERT_TRUE(allocation);
    EXPECT_CALL(vulkan, vkFlushMappedMemoryRanges(_, 1, NotNull()))
        .WillOnce([memory](VkDevice, std::uint32_t, const VkMappedMemoryRange* range) {
            EXPECT_EQ(range->memory, memory);
            EXPECT_EQ(range->offset, 128);
            EXPECT_EQ(range->size, 128);
            return exhausted;
        });
    const auto flushed = allocation->flush(1, 64);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(flushed.error().native);
    const auto native = flushed.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "Vulkan");
    EXPECT_EQ(native.code, exhausted);
}

TEST_F(MemoryPoolTest, WrapperInvalidationUsesLiveSliceAndPropagatesDeviceLoss)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 1, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_CALL(vulkan, vkInvalidateMappedMemoryRanges(_, 1, NotNull()))
        .WillOnce([memory](VkDevice, std::uint32_t, const VkMappedMemoryRange* range) {
            EXPECT_EQ(range->memory, memory);
            EXPECT_EQ(range->offset, 0);
            EXPECT_EQ(range->size, 128);
            return success;
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));
    ASSERT_TRUE(allocation->invalidate(1, 64));
    const auto failed = allocation->invalidate(0, 1);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::DeviceLost);
    EXPECT_EQ(pool->getStats().liveBytes, 128);
    const auto invalid = allocation->invalidate(127, 2);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MemoryPoolTest, CoherentWrapperValidatesRangesWithoutNativeCacheCalls)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 2, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest(65);
    request.requirements.memoryTypeBits = 4;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    ASSERT_TRUE(allocation->flush(1, 64));
    ASSERT_TRUE(allocation->invalidate(1, 64));
    EXPECT_FALSE(allocation->flush(64, 2));
    EXPECT_FALSE(allocation->invalidate(0, 0));
}

TEST_F(MemoryPoolTest, ExplicitPreferenceOutranksTheCoherenceTieBreaker)
{
    memoryProperties.memoryTypes[1].propertyFlags |=
        static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eHostCached);
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 1, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest();
    request.requirements.memoryTypeBits = 6;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    request.preferredProperties = vk::MemoryPropertyFlagBits::eHostCached;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
}

TEST_F(MemoryPoolTest, EqualPreferenceScoresKeepTheFirstCompatibleType)
{
    memoryProperties.memoryTypes[1] = memoryProperties.memoryTypes[2];
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(8 * mebibyte, 1, memory);
    expectMapped(memory, 8 * mebibyte);
    auto request = bufferRequest();
    request.requirements.memoryTypeBits = 6;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    request.preferredProperties = vk::MemoryPropertyFlagBits::eHostCoherent;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
}

TEST_F(MemoryPoolTest, UnsupportedFeatureCandidatesAreSkippedBeforeRanking)
{
    const auto flags = std::array {vk::MemoryPropertyFlagBits::eProtected,
                                   vk::MemoryPropertyFlagBits::eLazilyAllocated,
                                   vk::MemoryPropertyFlagBits::eDeviceCoherentAMD};
    for (const auto flag : flags)
    {
        memoryProperties.memoryTypes[0].propertyFlags =
            static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal | flag);
        memoryProperties.memoryTypes[1].propertyFlags =
            static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal);
        auto pool = MemoryPool::create(physical, device);
        auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
        expectAllocation(64 * mebibyte, 1, memory);
        EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
        auto request = bufferRequest();
        request.requirements.memoryTypeBits = 3;
        request.preferredProperties = flag;
        auto allocation = pool->allocate(request);
        ASSERT_TRUE(allocation);
    }
}

TEST_F(MemoryPoolTest, HeapBudgetsAreIndependentWhileBothHeapsHaveLiveAllocations)
{
    configureTwoHeaps();
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const secondMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(1024, 0, firstMemory);
    expectAllocation(2048, 1, secondMemory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, firstMemory, nullptr));
    EXPECT_CALL(vulkan, vkFreeMemory(_, secondMemory, nullptr));
    auto first = pool->allocate(bufferRequest(512));
    auto second = pool->allocate(bufferRequest(512));
    auto request = bufferRequest(1024);
    request.requirements.memoryTypeBits = 2;
    auto third = pool->allocate(request);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    EXPECT_FALSE(pool->allocate(bufferRequest(64)));
    auto fourth = pool->allocate(request);
    ASSERT_TRUE(fourth);
    EXPECT_EQ(pool->getStats().committedBytes, 3072);
    EXPECT_EQ(pool->getStats().liveBytes, 3072);
    EXPECT_EQ(pool->getStats().allocationCount, 2);
}

TEST_F(MemoryPoolTest, CacheReclamationReturnsBytesToTheCorrectHeap)
{
    configureTwoHeaps();
    auto pool = MemoryPool::create(physical, device);
    auto* const firstMemory = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const secondMemory = fakeVulkanHandle<VkDeviceMemory>(5);
    auto* const replacement = fakeVulkanHandle<VkDeviceMemory>(6);
    expectAllocation(1024, 0, firstMemory);
    expectAllocation(2048, 1, secondMemory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, firstMemory, nullptr));
    EXPECT_CALL(vulkan, vkFreeMemory(_, secondMemory, nullptr));
    auto first = pool->allocate(bufferRequest(512));
    auto request = bufferRequest(1024);
    request.requirements.memoryTypeBits = 2;
    auto second = pool->allocate(request);
    auto third = pool->allocate(request);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    first->reset();
    request.requirements.size = 64;
    EXPECT_FALSE(pool->allocate(request));
    EXPECT_EQ(pool->getStats().committedBytes, 2048);
    EXPECT_EQ(second->getMemory(), vk::DeviceMemory {secondMemory});
    expectAllocation(1024, 0, replacement);
    EXPECT_CALL(vulkan, vkFreeMemory(_, replacement, nullptr));
    auto reused = pool->allocate(bufferRequest(512));
    ASSERT_TRUE(reused);
    EXPECT_EQ(pool->getStats().committedBytes, 3072);
}

TEST_F(MemoryPoolTest, AtomPlacementFailureFreesTheTemporaryBlockBeforeDedicatedFallback)
{
    heapSize = 32;
    auto pool = MemoryPool::create(physical, device);
    auto* const temporary = fakeVulkanHandle<VkDeviceMemory>(4);
    auto* const fallback = fakeVulkanHandle<VkDeviceMemory>(5);
    expectAllocation(32, 1, temporary);
    const auto freed = expectMapped(temporary, 32, 0);
    EXPECT_CALL(vulkan, vkAllocateMemory(_, NotNull(), nullptr, NotNull()))
        .InSequence(allocationOrder)
        .After(freed)
        .WillOnce([fallback](VkDevice,
                             const VkMemoryAllocateInfo* info,
                             const VkAllocationCallbacks*,
                             VkDeviceMemory* output) {
            verifyAllocateInfo(*info, 16, 1, true);
            *output = fallback;
            return success;
        });
    expectMapped(fallback, 16, 1);
    auto request = bufferRequest(16);
    request.requirements.memoryTypeBits = 2;
    request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
    auto allocation = pool->allocate(request);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getMemory(), vk::DeviceMemory {fallback});
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().committedBytes, 16);
    EXPECT_EQ(pool->getStats().liveBytes, 16);
}

using MemoryPoolDeathTest = MemoryPoolTest;

TEST_F(MemoryPoolDeathTest, RejectsPoolDestructionBeforeAllocationOwners)
{
    auto pool = MemoryPool::create(physical, device);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(4);
    expectAllocation(64 * mebibyte, 0, memory);
    EXPECT_CALL(vulkan, vkFreeMemory(_, memory, nullptr));
    auto allocation = pool->allocate(bufferRequest());
    ASSERT_TRUE(allocation);
    EXPECT_DEATH(pool.reset(), "Memory pool destroyed with live allocation owners");
}

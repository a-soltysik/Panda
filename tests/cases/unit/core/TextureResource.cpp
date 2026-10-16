// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "TextureResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
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
using panda::detail::TextureResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto description(vk::Format format = vk::Format::eR8G8B8A8Unorm) -> panda::TextureDesc
{
    return {
        .extent = {.width = 32, .height = 16},
        .format = format,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    };
}

class TextureResourceTest : public testing::Test
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

    void expectImage(VkImage image, const panda::TextureDesc& desc, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkCreateImage(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([image,
                       desc,
                       result](VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* output) {
                EXPECT_EQ(info->imageType, VK_IMAGE_TYPE_2D);
                EXPECT_EQ(info->format, static_cast<VkFormat>(desc.format));
                EXPECT_EQ(info->extent.width, desc.extent.width);
                EXPECT_EQ(info->extent.height, desc.extent.height);
                EXPECT_EQ(info->extent.depth, 1);
                EXPECT_EQ(info->usage, static_cast<VkImageUsageFlags>(desc.usage));
                if (result == success)
                {
                    *output = image;
                }
                return result;
            });
    }

    void expectRequirements(VkImage image)
    {
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(deviceHandle, NotNull(), NotNull()))
            .WillOnce([image](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                EXPECT_EQ(info->image, image);
                output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 1};
                auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(dedicated, nullptr);
                dedicated->requiresDedicatedAllocation = vk::False;
            });
    }

    void expectBind(VkImage image, VkDeviceMemory memory, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkBindImageMemory2(deviceHandle, 1, NotNull()))
            .WillOnce([image, memory, result](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, 32);
                return result;
            });
    }

    void expectView(VkImage image, VkImageView view, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([image, view, result](VkDevice,
                                            const VkImageViewCreateInfo* info,
                                            const VkAllocationCallbacks*,
                                            VkImageView* output) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D);
                EXPECT_EQ(info->subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
                if (result == success)
                {
                    *output = view;
                }
                return result;
            });
    }

    void expectBoundImage(VkImage image)
    {
        expectImage(image, description());
        expectRequirements(image);
        EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
            .WillRepeatedly([](MemoryPool&, const panda::core::vulkan::resources::MemoryRequest&) {
                return std::uint64_t {9};
            });
        EXPECT_CALL(allocationMock, getMemory(9)).WillRepeatedly([](std::uint64_t) {
            return vk::DeviceMemory {fakeVulkanHandle<VkDeviceMemory>(30)};
        });
        EXPECT_CALL(allocationMock, getRange(9)).WillRepeatedly([](std::uint64_t) {
            return panda::core::vulkan::resources::MemoryRange {.offset = 32, .size = 128};
        });
        expectBind(image, fakeVulkanHandle<VkDeviceMemory>(30));
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
};
}

TEST_F(TextureResourceTest, RejectsInvalidDescriptionsBeforeCreatingNativeObjects)
{
    const auto empty = TextureResource::create(
        device,
        *pool,
        {.extent = {}, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled});
    const auto undefined = TextureResource::create(device,
                                                   *pool,
                                                   {
                                                       .extent = {.width = 1, .height = 1},
                                                       .format = vk::Format::eUndefined,
                                                       .usage = vk::ImageUsageFlagBits::eSampled
    });

    ASSERT_FALSE(empty);
    ASSERT_FALSE(undefined);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(undefined.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(TextureResourceTest, PreservesImageCreationFailureWithoutDestroyingItsUndefinedOutput)
{
    expectImage(fakeVulkanHandle<VkImage>(20), description(), exhausted);

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, DestroysAnUnboundImageWhenPoolAllocationFails)
{
    auto* const image = fakeVulkanHandle<VkImage>(20);
    expectImage(image, description());
    expectRequirements(image);
    EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
        .WillOnce(Return(panda::Result<std::uint64_t> {std::unexpected {
            panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled image allocation failure")}}));
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr));

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, DestroysBoundImageBeforeReleasingAllocationWhenViewCreationFails)
{
    auto* const image = fakeVulkanHandle<VkImage>(20);
    auto* const view = fakeVulkanHandle<VkImageView>(21);
    expectBoundImage(image);
    expectView(image, view, exhausted);
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).Times(0);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, OwnsItsViewImageAndAllocationAndTracksBatchReservations)
{
    auto* const image = fakeVulkanHandle<VkImage>(20);
    auto* const view = fakeVulkanHandle<VkImageView>(21);
    expectBoundImage(image);
    expectView(image, view);
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    {
        auto created = TextureResource::create(device, *pool, description());
        ASSERT_TRUE(created);
        EXPECT_EQ((*created)->getExtent(), (vk::Extent2D {.width = 32, .height = 16}));
        EXPECT_EQ((*created)->getFormat(), vk::Format::eR8G8B8A8Unorm);
        EXPECT_EQ((*created)->getUsage(), description().usage);
        EXPECT_EQ((*created)->getNative(), vk::Image {image});

        const auto firstBatch = panda::core::vulkan::upload::UploadBatchId {1};
        const auto otherBatch = panda::core::vulkan::upload::UploadBatchId {2};
        ASSERT_TRUE((*created)->reserveUpload(firstBatch));
        EXPECT_FALSE((*created)->reserveUpload(otherBatch));
        (*created)->cancelUpload(firstBatch);
        ASSERT_TRUE((*created)->reserveUpload(otherBatch));
        (*created)->commitUpload(otherBatch);
        EXPECT_FALSE((*created)->reserveUpload(firstBatch));
    }
}

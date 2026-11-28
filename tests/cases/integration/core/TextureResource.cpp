// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "TextureResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <panda/base/Error.hpp>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::TextureResource;
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;
constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);
constexpr auto mebibyte = std::uint64_t {1024} * 1024;

struct TextureViewCase final
{
    vk::Format format;
    VkImageAspectFlags aspect;
};

void verifyViewRange(const VkImageViewCreateInfo& info, VkImageAspectFlags aspect)
{
    EXPECT_EQ(info.subresourceRange.baseMipLevel, 0U);
    EXPECT_EQ(info.subresourceRange.aspectMask, aspect);
    EXPECT_EQ(info.subresourceRange.levelCount, 1U);
    EXPECT_EQ(info.subresourceRange.baseArrayLayer, 0U);
    EXPECT_EQ(info.subresourceRange.layerCount, 1U);
}

void verifyViewInfo(const VkImageViewCreateInfo& info, VkImage image, vk::Format format, VkImageAspectFlags aspect)
{
    EXPECT_EQ(info.image, image);
    EXPECT_EQ(info.viewType, VK_IMAGE_VIEW_TYPE_2D);
    EXPECT_EQ(info.format, static_cast<VkFormat>(format));
    EXPECT_EQ(info.components.r, VK_COMPONENT_SWIZZLE_IDENTITY);
    EXPECT_EQ(info.components.g, VK_COMPONENT_SWIZZLE_IDENTITY);
    EXPECT_EQ(info.components.b, VK_COMPONENT_SWIZZLE_IDENTITY);
    EXPECT_EQ(info.components.a, VK_COMPONENT_SWIZZLE_IDENTITY);
    verifyViewRange(info, aspect);
}

class TextureResourceTest : public panda::test::VulkanTestFixture
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(physicalHandle, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                if (output == nullptr)
                {
                    ADD_FAILURE() << "Memory properties output is null";
                    return;
                }
                *output = VkPhysicalDeviceMemoryProperties {};
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = mebibyte, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(physicalHandle, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                if (output == nullptr || output->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 ||
                    output->pNext == nullptr)
                {
                    ADD_FAILURE() << "Device properties output or pNext is invalid";
                    return;
                }
                const auto* header = static_cast<const VkBaseOutStructure*>(output->pNext);
                if (header->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES)
                {
                    ADD_FAILURE() << "Device properties pNext has an unexpected structure type";
                    return;
                }
                output->properties.limits.maxMemoryAllocationCount = 16;
                output->properties.limits.nonCoherentAtomSize = 1;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = mebibyte;
            });
        pool = MemoryPool::create(physical, device);
    }

    void expectImageCreation(VkImage image, vk::Format format, vk::ImageUsageFlags usage)
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(physicalHandle,
                                                             static_cast<VkFormat>(format),
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             static_cast<VkImageUsageFlags>(usage),
                                                             0,
                                                             NotNull()))
            .InSequence(creationOrder)
            .WillOnce([](VkPhysicalDevice,
                         VkFormat,
                         VkImageType,
                         VkImageTiling,
                         VkImageUsageFlags,
                         VkImageCreateFlags,
                         VkImageFormatProperties* output) {
                if (output == nullptr)
                {
                    ADD_FAILURE() << "Image format properties output is null";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                *output = VkImageFormatProperties {};
                output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                return success;
            });
        EXPECT_CALL(vulkan, vkCreateImage(deviceHandle, NotNull(), nullptr, NotNull()))
            .InSequence(creationOrder)
            .WillOnce([image,
                       format](VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* output) {
                if (info == nullptr || output == nullptr || info->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)
                {
                    ADD_FAILURE() << "Image create info or output is invalid";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                EXPECT_EQ(info->imageType, VK_IMAGE_TYPE_2D);
                EXPECT_EQ(info->format, static_cast<VkFormat>(format));
                EXPECT_EQ(info->extent.depth, 1U);
                EXPECT_EQ(info->mipLevels, 1U);
                EXPECT_EQ(info->arrayLayers, 1U);
                EXPECT_EQ(info->tiling, VK_IMAGE_TILING_OPTIMAL);
                *output = image;
                return success;
            });
    }

    void expectImageMemoryRequirements(VkImage image)
    {
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(deviceHandle, NotNull(), NotNull()))
            .InSequence(creationOrder)
            .WillOnce([image](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                if (info == nullptr || info->sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 ||
                    output == nullptr || output->sType != VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 ||
                    output->pNext == nullptr)
                {
                    ADD_FAILURE() << "Image requirements info or output chain is invalid";
                    return;
                }
                const auto* header = static_cast<const VkBaseOutStructure*>(output->pNext);
                if (header->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)
                {
                    ADD_FAILURE() << "Image requirements pNext has an unexpected structure type";
                    return;
                }
                EXPECT_EQ(info->image, image);
                output->memoryRequirements = {.size = 128, .alignment = 16, .memoryTypeBits = 1};
                auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(dedicated, nullptr);
                dedicated->requiresDedicatedAllocation = VK_TRUE;
            });
    }

    void expectImageAllocation(VkImage image, VkDeviceMemory memory)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(deviceHandle, NotNull(), nullptr, NotNull()))
            .InSequence(creationOrder)
            .WillOnce([image, memory](VkDevice,
                                      const VkMemoryAllocateInfo* info,
                                      const VkAllocationCallbacks*,
                                      VkDeviceMemory* output) {
                if (info == nullptr || output == nullptr || info->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO)
                {
                    ADD_FAILURE() << "Memory allocation info or output is invalid";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                EXPECT_EQ(info->allocationSize, 128U);
                EXPECT_NE(info->pNext, nullptr);
                if (info->pNext == nullptr)
                {
                    return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
                }
                const auto* header = static_cast<const VkBaseOutStructure*>(info->pNext);
                if (header->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO)
                {
                    ADD_FAILURE() << "Dedicated allocation pNext has an unexpected structure type";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                const auto* dedicated = static_cast<const VkMemoryDedicatedAllocateInfo*>(info->pNext);
                EXPECT_EQ(dedicated->image, image);
                *output = memory;
                return success;
            });
    }

    void expectImageBinding(VkImage image, VkDeviceMemory memory)
    {
        EXPECT_CALL(vulkan, vkBindImageMemory2(deviceHandle, 1, NotNull()))
            .InSequence(creationOrder)
            .WillOnce([image, memory](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                if (info == nullptr || info->sType != VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO)
                {
                    ADD_FAILURE() << "Image bind info is invalid";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, 0U);
                return success;
            });
    }

    void expectViewCreation(
        VkImage image, VkImageView view, vk::Format format, VkImageAspectFlags aspect, VkResult result)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(deviceHandle, NotNull(), nullptr, NotNull()))
            .InSequence(creationOrder)
            .WillOnce([image, view, format, aspect, result](VkDevice,
                                                            const VkImageViewCreateInfo* info,
                                                            const VkAllocationCallbacks*,
                                                            VkImageView* output) {
                if (info == nullptr || output == nullptr || info->sType != VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO)
                {
                    ADD_FAILURE() << "Image view info or output is invalid";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                verifyViewInfo(*info, image, format, aspect);
                *output = view;
                return result;
            });
    }

    void expectNativeImage(VkImage image,
                           VkImageView view,
                           VkDeviceMemory memory,
                           vk::Format format,
                           VkImageAspectFlags aspect,
                           VkResult viewResult = success)
    {
        expectImageCreation(image, format, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        expectImageMemoryRequirements(image);
        expectImageAllocation(image, memory);
        expectImageBinding(image, memory);
        expectViewCreation(image, view, format, aspect, viewResult);
    }

    std::unique_ptr<MemoryPool> pool;
    testing::Sequence creationOrder;
};

class TextureResourceAspectTest : public TextureResourceTest, public testing::WithParamInterface<TextureViewCase>
{
};
}

TEST_P(TextureResourceAspectTest, CreatesDefaultViewWithTheFormatAspectAndDestroysItBeforeTheImage)
{
    const auto viewCase = GetParam();
    auto* const image = fakeHandle<VkImage>(10);
    auto* const view = fakeHandle<VkImageView>(11);
    auto* const memory = fakeHandle<VkDeviceMemory>(12);
    expectNativeImage(image, view, memory, viewCase.format, viewCase.aspect);
    auto cleanupOrder = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).InSequence(cleanupOrder);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanupOrder);
    EXPECT_CALL(vulkan, vkFreeMemory(deviceHandle, memory, nullptr)).InSequence(cleanupOrder);

    auto resource = TextureResource::createNative(
        device,
        *pool,
        {
            .extent = vk::Extent2D {.width = 32, .height = 16},
            .format = viewCase.format,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    });

    ASSERT_TRUE(resource);
    resource->reset();
    pool.reset();
}

INSTANTIATE_TEST_SUITE_P(
    DefaultImageAspect,
    TextureResourceAspectTest,
    testing::Values(TextureViewCase {.format = vk::Format::eR8G8B8A8Unorm, .aspect = VK_IMAGE_ASPECT_COLOR_BIT},
                    TextureViewCase {.format = vk::Format::eD16Unorm, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eX8D24UnormPack32, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eD32Sfloat, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eD16UnormS8Uint, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eD24UnormS8Uint, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eD32SfloatS8Uint, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
                    TextureViewCase {.format = vk::Format::eS8Uint, .aspect = VK_IMAGE_ASPECT_STENCIL_BIT}));

TEST_F(TextureResourceTest, FailedViewCreationDoesNotAdoptItsOutputAndDestroysTheBoundImage)
{
    auto* const image = fakeHandle<VkImage>(20);
    auto* const view = fakeHandle<VkImageView>(21);
    auto* const memory = fakeHandle<VkDeviceMemory>(22);
    const auto format = vk::Format::eR8G8B8A8Unorm;
    expectNativeImage(image, view, memory, format, VK_IMAGE_ASPECT_COLOR_BIT, exhausted);
    EXPECT_CALL(vulkan, vkDestroyImageView(_, _, _)).Times(0);
    auto cleanupOrder = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanupOrder);
    EXPECT_CALL(vulkan, vkFreeMemory(deviceHandle, memory, nullptr)).InSequence(cleanupOrder);

    const auto resource = TextureResource::createNative(
        device,
        *pool,
        {
            .extent = vk::Extent2D {.width = 32, .height = 16},
            .format = format,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    });

    ASSERT_FALSE(resource);
    EXPECT_EQ(resource.error().code, panda::ErrorCode::ResourceExhausted);
    pool.reset();
}

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/RenderAttachments.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <panda/Error.hpp>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/Texture.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
using panda::core::vulkan::renderer::RenderAttachments;
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::TextureResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::AnyNumber;
using testing::NotNull;

class RenderAttachmentsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        expectMemoryProperties();
        expectPhysicalDeviceProperties();
        expectImageFormatProperties();
        expectTextureResourceCalls();
        expectPipelineBarrierCalls();
        pool = MemoryPool::create(physical, device);
    }

    void expectMemoryProperties()
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
    }

    void expectPhysicalDeviceProperties()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                properties->properties.limits.maxMemoryAllocationCount = 8;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(properties->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
                constexpr auto sampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                              static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                              static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);
                properties->limits.framebufferColorSampleCounts = sampleCounts;
                properties->limits.framebufferDepthSampleCounts = sampleCounts;
            });
    }

    void expectImageFormatProperties()
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
            .Times(AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice,
                               VkFormat format,
                               VkImageType,
                               VkImageTiling,
                               VkImageUsageFlags,
                               VkImageCreateFlags,
                               VkImageFormatProperties* properties) {
                *properties = VkImageFormatProperties {};
                properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                constexpr auto multisampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                                   static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                                   static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);
                properties->sampleCounts = format == VK_FORMAT_B8G8R8A8_UNORM
                                               ? static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT)
                                               : multisampleCounts;
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void expectTextureResourceCalls()
    {
        EXPECT_CALL(textures, create(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly([this](const vk::raii::Device&, MemoryPool&, const panda::TextureDesc& description) {
                createdUsages.push_back(description.usage);
                if (failColorAt128 && description.extent.width == 128 &&
                    description.usage == vk::ImageUsageFlagBits::eColorAttachment)
                {
                    return panda::Result<void> {std::unexpected {
                        panda::makeError(panda::ErrorCode::ResourceExhausted, "color allocation failed")}};
                }
                return panda::Result<void> {};
            });
        EXPECT_CALL(textures, getNative(_)).Times(AnyNumber()).WillRepeatedly([this](const TextureResource& texture) {
            auto [entry, inserted] = images.try_emplace(&texture, fakeVulkanHandle<VkImage>(nextHandle));
            if (inserted)
            {
                ++nextHandle;
            }
            return entry->second;
        });
        EXPECT_CALL(textures, getNativeView(_))
            .Times(AnyNumber())
            .WillRepeatedly([this](const TextureResource& texture) {
                auto [entry, inserted] = views.try_emplace(&texture, fakeVulkanHandle<VkImageView>(nextHandle));
                if (inserted)
                {
                    ++nextHandle;
                }
                return entry->second;
            });
    }

    void expectPipelineBarrierCalls()
    {
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkCommandBuffer, const VkDependencyInfo* dependency) {
                ASSERT_EQ(dependency->imageMemoryBarrierCount, 1U);
                barriers.push_back(*dependency->pImageMemoryBarriers);
            });
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto createAttachments(vk::Extent2D extent = {.width = 64, .height = 64},
                           vk::Format format = vk::Format::eR8G8B8A8Unorm,
                           std::size_t frameCount = 1)
        -> std::expected<std::unique_ptr<RenderAttachments>, panda::Error>
    {
        return RenderAttachments::create({.physicalDevice = physical,
                                          .device = device,
                                          .pool = *pool,
                                          .extent = extent,
                                          .colorFormat = format,
                                          .frameCount = frameCount});
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::TextureResourceMock> textures;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::vector<vk::ImageUsageFlags> createdUsages;
    std::map<const TextureResource*, VkImage> images;
    std::map<const TextureResource*, VkImageView> views;
    std::vector<VkImageMemoryBarrier2> barriers;
    std::uintptr_t nextHandle {10};
    bool failColorAt128 {false};
};
}

TEST_F(RenderAttachmentsTest, NoOpFormatAndExtentResizePreserveOnlyUnaffectedResourceHistory)
{
    auto attachments = createAttachments();
    ASSERT_TRUE(attachments);
    (*attachments)->markSubmitted(0);
    const auto initialCreateCount = createdUsages.size();
    const auto shadowView = (*attachments)->getShadowView(0);

    auto noOp = (*attachments)->prepareResize({.width = 64, .height = 64}, vk::Format::eR8G8B8A8Unorm);
    ASSERT_TRUE(noOp);
    (*attachments)->publishResize(std::move(*noOp));
    EXPECT_EQ(createdUsages.size(), initialCreateCount);
    (*attachments)->transitionColor(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionDepth(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionShadowForAttachment(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    ASSERT_EQ(barriers.size(), 3U);
    EXPECT_EQ(barriers.at(0).oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(1).oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(2).oldLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

    const auto createsBeforeFormatChange = createdUsages.size();
    auto formatChange = (*attachments)->prepareResize({.width = 64, .height = 64}, vk::Format::eR8G8B8A8Srgb);
    ASSERT_TRUE(formatChange);
    ASSERT_EQ(createdUsages.size(), createsBeforeFormatChange + 1);
    EXPECT_EQ(createdUsages.back(), vk::ImageUsageFlagBits::eColorAttachment);
    (*attachments)->publishResize(std::move(*formatChange));
    (*attachments)->transitionColor(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionDepth(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionShadowForAttachment(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    ASSERT_EQ(barriers.size(), 6U);
    EXPECT_EQ(barriers.at(3).oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barriers.at(4).oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(5).oldLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

    const auto createsBeforeExtentChange = createdUsages.size();
    auto extentChange = (*attachments)->prepareResize({.width = 128, .height = 64}, vk::Format::eR8G8B8A8Srgb);
    ASSERT_TRUE(extentChange);
    ASSERT_EQ(createdUsages.size(), createsBeforeExtentChange + 2);
    EXPECT_EQ(createdUsages.at(createsBeforeExtentChange), vk::ImageUsageFlagBits::eDepthStencilAttachment);
    EXPECT_EQ(createdUsages.at(createsBeforeExtentChange + 1), vk::ImageUsageFlagBits::eColorAttachment);
    (*attachments)->publishResize(std::move(*extentChange));
    (*attachments)->transitionColor(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionDepth(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionShadowForAttachment(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    ASSERT_EQ(barriers.size(), 9U);
    EXPECT_EQ(barriers.at(6).oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barriers.at(7).oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barriers.at(8).oldLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    EXPECT_EQ((*attachments)->getShadowView(0), shadowView);
}

TEST_F(RenderAttachmentsTest, FailedSecondReplacementKeepsPublishedResourcesAndDescription)
{
    auto attachments = createAttachments();
    ASSERT_TRUE(attachments);
    const auto initialDescription = (*attachments)->getOutputDescription();
    const auto shadowView = (*attachments)->getShadowView(0);
    (*attachments)->markSubmitted(0);
    const auto initialCreateCount = createdUsages.size();
    failColorAt128 = true;

    const auto resized = (*attachments)->prepareResize({.width = 128, .height = 64}, vk::Format::eR8G8B8A8Unorm);

    ASSERT_FALSE(resized);
    EXPECT_EQ(resized.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_EQ(createdUsages.size(), initialCreateCount + 2);
    EXPECT_EQ(createdUsages.at(initialCreateCount), vk::ImageUsageFlagBits::eDepthStencilAttachment);
    EXPECT_EQ(createdUsages.at(initialCreateCount + 1), vk::ImageUsageFlagBits::eColorAttachment);
    const auto& current = (*attachments)->getOutputDescription();
    EXPECT_EQ(current.extent.width, initialDescription.extent.width);
    EXPECT_EQ(current.extent.height, initialDescription.extent.height);
    EXPECT_EQ(current.colorFormat, initialDescription.colorFormat);
    EXPECT_EQ(current.sampleCount, initialDescription.sampleCount);
    (*attachments)->transitionDepth(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    (*attachments)->transitionShadowForAttachment(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    ASSERT_EQ(barriers.size(), 2U);
    EXPECT_EQ(barriers.at(0).oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(1).oldLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    EXPECT_EQ((*attachments)->getShadowView(0), shadowView);
}

TEST_F(RenderAttachmentsTest, RecordingDoesNotCommitLayoutHistoryUntilSubmission)
{
    auto attachments = createAttachments();
    ASSERT_TRUE(attachments);
    const auto command = vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)};
    for (auto attempt = 0; attempt < 2; ++attempt)
    {
        (*attachments)->transitionColor(command, 0);
        (*attachments)->transitionDepth(command, 0);
        (*attachments)->transitionShadowForAttachment(command, 0);
        (*attachments)->transitionShadowForSampling(command, 0);
    }
    ASSERT_EQ(barriers.size(), 8U);
    for (const auto index : std::array<std::size_t, 4> {0, 1, 4, 5})
    {
        EXPECT_EQ(barriers.at(index).oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    }

    (*attachments)->markSubmitted(0);
    (*attachments)->transitionColor(command, 0);
    (*attachments)->transitionDepth(command, 0);
    (*attachments)->transitionShadowForAttachment(command, 0);
    ASSERT_EQ(barriers.size(), 11U);
    EXPECT_EQ(barriers.at(8).oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(9).oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barriers.at(10).oldLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

TEST_F(RenderAttachmentsTest, ClearsOptionalMsaaColorAttachmentWhenOutputUsesOneSample)
{
    auto attachments = createAttachments();
    ASSERT_TRUE(attachments);
    const auto colorCount = std::ranges::count(createdUsages, vk::ImageUsageFlagBits::eColorAttachment);
    auto pending = (*attachments)->prepareResize({.width = 64, .height = 64}, vk::Format::eB8G8R8A8Unorm);
    ASSERT_TRUE(pending);
    EXPECT_EQ(pending->description.sampleCount, vk::SampleCountFlagBits::e1);
    (*attachments)->publishResize(std::move(*pending));
    EXPECT_EQ((*attachments)->getColorView(0), vk::ImageView {});
    (*attachments)->transitionColor(vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(4)}, 0);
    EXPECT_EQ(std::ranges::count(createdUsages, vk::ImageUsageFlagBits::eColorAttachment), colorCount);
    EXPECT_EQ(barriers.size(), 0U);
}

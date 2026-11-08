// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBindings.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
using panda::core::vulkan::renderer::FrameBindings;
using panda::core::vulkan::renderer::MaterialRecord;
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::BufferResource;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::AnyNumber;
using testing::NotNull;
using testing::Ref;
using testing::Return;

class FrameBindingsTest : public testing::Test
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
        pool = MemoryPool::create(physical, device);

        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
                properties->limits.maxStorageBufferRange = 4096;
            });
        installDescriptorDefaults();
        EXPECT_CALL(buffers, create(_, Ref(*pool), _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(panda::Result<void> {}));
        EXPECT_CALL(buffers, getNative(_)).Times(AnyNumber()).WillRepeatedly([this](const BufferResource&) {
            return fakeVulkanHandle<VkBuffer>(nextHandle++);
        });
        EXPECT_CALL(buffers, writeMapped(_, _, _)).Times(AnyNumber()).WillRepeatedly(Return(panda::Result<void> {}));
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto createBindings() -> std::expected<std::unique_ptr<FrameBindings>, panda::Error>
    {
        return FrameBindings::create(physical, device, *pool, 1);
    }

    static auto makeView(std::size_t drawCount) -> panda::RenderView
    {
        auto view = panda::RenderView {};
        view.camera.position = {0.0F, 0.0F, 1.0F};
        view.meshes.resize(drawCount);
        return view;
    }

    void installDescriptorDefaults()
    {
        installDescriptorLayoutDefaults();
        installDescriptorPoolDefaults();
        installSamplerDefaults();
    }

    void installDescriptorLayoutDefaults()
    {
        EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, NotNull(), _, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkDescriptorSetLayoutCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkDescriptorSetLayout* output) {
                EXPECT_GT(info->bindingCount, 0U);
                *output = fakeVulkanHandle<VkDescriptorSetLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkCreatePipelineLayout(_, NotNull(), _, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkPipelineLayoutCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkPipelineLayout* output) {
                EXPECT_EQ(info->setLayoutCount, 2U);
                *output = fakeVulkanHandle<VkPipelineLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void installDescriptorPoolDefaults()
    {
        EXPECT_CALL(vulkan, vkCreateDescriptorPool(_, NotNull(), _, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkDescriptorPoolCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkDescriptorPool* output) {
                EXPECT_GT(info->maxSets, 0U);
                *output = fakeVulkanHandle<VkDescriptorPool>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkAllocateDescriptorSets(_, NotNull(), NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
                for (auto& descriptorSet : std::span {output, info->descriptorSetCount})
                {
                    descriptorSet = fakeVulkanHandle<VkDescriptorSet>(nextHandle++);
                }
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void installSamplerDefaults()
    {
        EXPECT_CALL(vulkan, vkCreateSampler(_, NotNull(), _, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly(
                [this](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* output) {
                    *output = fakeVulkanHandle<VkSampler>(nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkDestroyDescriptorSetLayout(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(vulkan, vkDestroyPipelineLayout(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(vulkan, vkDestroyDescriptorPool(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(vulkan, vkFreeDescriptorSets(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(vulkan, vkDestroySampler(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(vulkan, vkUpdateDescriptorSets(_, _, _, _, _)).Times(AnyNumber());
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::BufferResourceMock> buffers;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::uintptr_t nextHandle {10};
};
}

TEST_F(FrameBindingsTest, PacksMaterialValuesAndWritesFallbackTextureDescriptors)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    auto view = makeView(1);
    auto& material = view.meshes.front().material;
    material.model = panda::MaterialModel::Unlit;
    material.renderFace = panda::RenderFace::Both;
    material.metallic = 1.5F;
    material.roughness = -0.5F;
    material.alphaCutoff = 0.4F;
    material.emissive = {0.2F, 0.4F, 0.8F};
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(2)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });
    const auto shadowView = vk::ImageView {fakeVulkanHandle<VkImageView>(90)};
    EXPECT_CALL(vulkan, vkUpdateDescriptorSets(deviceHandle, 3, NotNull(), 0, nullptr))
        .WillOnce([shadowView](VkDevice,
                               std::uint32_t writeCount,
                               const VkWriteDescriptorSet* descriptorWrites,
                               std::uint32_t,
                               const VkCopyDescriptorSet*) {
            const auto writeSpan = std::span {descriptorWrites, writeCount};
            const auto imageWrite = std::ranges::find_if(writeSpan, [](const auto& write) {
                return write.pImageInfo != nullptr;
            });
            ASSERT_NE(imageWrite, writeSpan.end());
            EXPECT_EQ(imageWrite->pImageInfo->imageView, static_cast<VkImageView>(shadowView));
            EXPECT_EQ(imageWrite->pImageInfo->imageLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
        })
        .WillOnce([shadowView](VkDevice,
                               std::uint32_t writeCount,
                               const VkWriteDescriptorSet* descriptorWrites,
                               std::uint32_t,
                               const VkCopyDescriptorSet*) {
            for (const auto& write : std::span {descriptorWrites, writeCount})
            {
                EXPECT_NE(write.pImageInfo, nullptr);
                EXPECT_EQ(write.pImageInfo->imageView, static_cast<VkImageView>(shadowView));
                EXPECT_EQ(write.pImageInfo->imageLayout, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            }
        });

    const auto updated = bindings->updateFrame(0, view, shadowView);

    ASSERT_TRUE(updated);
    ASSERT_EQ(writes.size(), 2U);
    ASSERT_EQ(writes.back().size(), sizeof(MaterialRecord));
    auto record = MaterialRecord {};
    std::memcpy(&record, writes.back().data(), sizeof(record));
    EXPECT_FLOAT_EQ(record.metallic, 1.0F);
    EXPECT_FLOAT_EQ(record.roughness, 0.0F);
    EXPECT_EQ(record.isLit, 0U);
    EXPECT_EQ(record.renderFace, static_cast<std::uint32_t>(panda::RenderFace::Both));
    EXPECT_FLOAT_EQ(record.alphaCutoff, 0.4F);
    EXPECT_EQ(record.hasBaseColorTexture, 0U);
    EXPECT_EQ(record.hasMetallicRoughnessTexture, 0U);
    EXPECT_EQ(record.hasEmissiveTexture, 0U);
    EXPECT_FLOAT_EQ(record.emissive.x, 0.2F);
    EXPECT_FLOAT_EQ(record.emissive.y, 0.4F);
    EXPECT_FLOAT_EQ(record.emissive.z, 0.8F);
}

TEST_F(FrameBindingsTest, MaterialBufferGrowthFailureCanBeRetried)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    const auto largerBufferSize = sizeof(MaterialRecord) * 2;
    EXPECT_CALL(buffers, create(Ref(device), Ref(*pool), testing::Field(&panda::BufferDesc::size, largerBufferSize)))
        .Times(2)
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "buffer growth failed")}}))
        .WillOnce(Return(panda::Result<void> {}));
    auto view = makeView(2);
    const auto shadowView = vk::ImageView {fakeVulkanHandle<VkImageView>(90)};

    const auto failed = bindings->updateFrame(0, view, shadowView);
    const auto retried = bindings->updateFrame(0, view, shadowView);

    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(retried);
}

TEST_F(FrameBindingsTest, TextureDescriptorPoolGrowthFailureCanBeRetried)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    EXPECT_CALL(vulkan, vkCreateDescriptorPool(deviceHandle, NotNull(), _, NotNull()))
        .Times(2)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)))
        .WillOnce([this](VkDevice,
                         const VkDescriptorPoolCreateInfo*,
                         const VkAllocationCallbacks*,
                         VkDescriptorPool* output) {
            *output = fakeVulkanHandle<VkDescriptorPool>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    auto view = makeView(2);
    const auto shadowView = vk::ImageView {fakeVulkanHandle<VkImageView>(90)};

    const auto failed = bindings->updateFrame(0, view, shadowView);
    const auto retried = bindings->updateFrame(0, view, shadowView);

    EXPECT_FALSE(failed);
    EXPECT_TRUE(retried);
}

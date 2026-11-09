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
#include <glm/ext/matrix_float4x4.hpp>
#include <limits>
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
#include "panda/core/TextureResourceMock.hpp"
#include "renderer/MeshRenderer.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
constexpr auto allSampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                 static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                 static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);

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
                properties->limits.maxPushConstantsSize = 4096;
                properties->limits.maxStorageBufferRange = 4096;
                properties->limits.framebufferColorSampleCounts = allSampleCounts;
                properties->limits.framebufferDepthSampleCounts = allSampleCounts;
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

    auto meshRendererInfo() const -> panda::core::vulkan::renderer::MeshRendererCreateInfo
    {
        return {
            .physicalDevice = physical,
            .device = device,
            .pool = *pool,
            .extent = {.width = 64, .height = 64},
            .colorFormat = vk::Format::eR8G8B8A8Unorm,
            .frameCount = 1
        };
    }

    auto createMeshRenderer()
        -> std::expected<std::unique_ptr<panda::core::vulkan::renderer::MeshRenderer>, panda::Error>
    {
        return createMeshRenderer(allSampleCounts);
    }

    auto createMeshRenderer(VkSampleCountFlags supportedSamples)
        -> std::expected<std::unique_ptr<panda::core::vulkan::renderer::MeshRenderer>, panda::Error>
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
            .Times(AnyNumber())
            .WillRepeatedly([supportedSamples](VkPhysicalDevice,
                                               VkFormat,
                                               VkImageType,
                                               VkImageTiling,
                                               VkImageUsageFlags,
                                               VkImageCreateFlags,
                                               VkImageFormatProperties* properties) {
                *properties = VkImageFormatProperties {};
                properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                properties->sampleCounts = supportedSamples;
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(textures, create(Ref(device), Ref(*pool), _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(panda::Result<void> {}));
        EXPECT_CALL(textures, getNative(_)).Times(AnyNumber()).WillRepeatedly(Return(fakeVulkanHandle<VkImage>(91)));
        EXPECT_CALL(textures, getNativeView(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(fakeVulkanHandle<VkImageView>(90)));
        return panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());
    }

    static auto bufferFailure() -> panda::Result<void>
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "buffer write failed")};
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
    panda::test::ScopedMock<panda::test::TextureResourceMock> textures;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::uintptr_t nextHandle {10};
};
}

TEST(MeshRenderer, EncodesLinearClearForTheAttachmentFormat)
{
    const auto srgb = panda::core::vulkan::renderer::makeClearColor(vk::Format::eB8G8R8A8Srgb);
    const auto unorm = panda::core::vulkan::renderer::makeClearColor(vk::Format::eB8G8R8A8Unorm);
    EXPECT_NEAR(srgb.float32.at(0), 0.025F, 0.0001F);
    EXPECT_NEAR(unorm.float32.at(0), 0.1718F, 0.0001F);
    EXPECT_FLOAT_EQ(srgb.float32.at(3), 1.0F);
    EXPECT_FLOAT_EQ(unorm.float32.at(3), 1.0F);
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

TEST_F(FrameBindingsTest, RejectsInsufficientStorageLimitAndInvalidFrameCounts)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxStorageBufferRange = static_cast<std::uint32_t>(sizeof(MaterialRecord) - 1);
        })
        .RetiresOnSaturation();
    const auto unsupported = createBindings();
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);

    const auto empty = FrameBindings::create(physical, device, *pool, 0);
    const auto oversized =
        FrameBindings::create(physical,
                              device,
                              *pool,
                              static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1);
    EXPECT_FALSE(empty);
    EXPECT_FALSE(oversized);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(oversized.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(FrameBindingsTest, RejectsMaterialCountBeyondStorageBufferLimit)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxStorageBufferRange = static_cast<std::uint32_t>(sizeof(MaterialRecord));
        });
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);

    const auto updated = (*bindings)->updateFrame(0, makeView(2), {});

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, PropagatesInvalidShadowViewAndUniformWriteFailures)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    auto invalidView = makeView(0);
    invalidView.camera.viewProjection = glm::mat4 {0.0F};
    const auto invalid = (*bindings)->updateFrame(0, invalidView, {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);

    EXPECT_CALL(buffers, writeMapped(_, 0, _)).WillOnce(Return(bufferFailure()));
    const auto failedWrite = (*bindings)->updateFrame(0, makeView(0), {});
    ASSERT_FALSE(failedWrite);
    EXPECT_EQ(failedWrite.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, PropagatesMaterialWriteFailures)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(2)
        .WillOnce(Return(panda::Result<void> {}))
        .WillOnce(Return(bufferFailure()));

    const auto updated = (*bindings)->updateFrame(0, makeView(1), {});

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, UpdatesEmptyViewWithoutWritingMaterialRecords)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    EXPECT_CALL(buffers, writeMapped(_, 0, _)).Times(1);

    const auto updated = (*bindings)->updateFrame(0, makeView(0), {});

    EXPECT_TRUE(updated);
}

TEST_F(FrameBindingsTest, PropagatesDescriptorLayoutCreationFailures)
{
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, NotNull(), _, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto frameLayout = createBindings();
    ASSERT_FALSE(frameLayout);

    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, NotNull(), _, NotNull()))
        .Times(2)
        .WillOnce([](VkDevice,
                     const VkDescriptorSetLayoutCreateInfo*,
                     const VkAllocationCallbacks*,
                     VkDescriptorSetLayout* output) {
            *output = fakeVulkanHandle<VkDescriptorSetLayout>(40);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto textureLayout = createBindings();
    EXPECT_FALSE(textureLayout);
}

TEST_F(FrameBindingsTest, PropagatesPipelineLayoutAndDescriptorPoolCreationFailures)
{
    EXPECT_CALL(vulkan, vkCreatePipelineLayout(_, NotNull(), _, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)))
        .RetiresOnSaturation();
    const auto layout = createBindings();
    ASSERT_FALSE(layout);

    EXPECT_CALL(vulkan, vkCreateDescriptorPool(deviceHandle, NotNull(), _, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto descriptorPool = createBindings();
    EXPECT_FALSE(descriptorPool);
}

TEST_F(FrameBindingsTest, PropagatesFrameDescriptorSetAllocationFailure)
{
    EXPECT_CALL(vulkan, vkAllocateDescriptorSets(deviceHandle, NotNull(), NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfPoolMemory)));

    const auto created = createBindings();

    EXPECT_FALSE(created);
}

TEST_F(FrameBindingsTest, PropagatesSamplerCreationFailures)
{
    EXPECT_CALL(vulkan, vkCreateSampler(deviceHandle, NotNull(), _, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto shadowSampler = createBindings();
    ASSERT_FALSE(shadowSampler);

    EXPECT_CALL(vulkan, vkCreateSampler(deviceHandle, NotNull(), _, NotNull()))
        .Times(2)
        .WillOnce([](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* output) {
            *output = fakeVulkanHandle<VkSampler>(50);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto textureSampler = createBindings();
    EXPECT_FALSE(textureSampler);
}

TEST_F(FrameBindingsTest, PropagatesUniformAndMaterialBufferCreationFailures)
{
    EXPECT_CALL(buffers, create(Ref(device), Ref(*pool), _)).WillOnce(Return(bufferFailure()));
    const auto uniformBuffer = createBindings();
    ASSERT_FALSE(uniformBuffer);

    EXPECT_CALL(buffers, create(Ref(device), Ref(*pool), _))
        .Times(2)
        .WillOnce(Return(panda::Result<void> {}))
        .WillOnce(Return(bufferFailure()));
    const auto materialBuffer = createBindings();
    EXPECT_FALSE(materialBuffer);
}

TEST_F(FrameBindingsTest, TextureDescriptorAllocationFailureCanBeRetried)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    EXPECT_CALL(vulkan, vkAllocateDescriptorSets(deviceHandle, NotNull(), NotNull()))
        .Times(2)
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfPoolMemory)))
        .WillOnce([this](VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
            for (auto& descriptorSet : std::span {output, info->descriptorSetCount})
            {
                descriptorSet = fakeVulkanHandle<VkDescriptorSet>(nextHandle++);
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    const auto view = makeView(1);

    const auto failed = (*bindings)->updateFrame(0, view, {});
    const auto retried = (*bindings)->updateFrame(0, view, {});

    EXPECT_FALSE(failed);
    EXPECT_TRUE(retried);
}

TEST_F(FrameBindingsTest, RejectsInvalidMeshRendererInputsAndDeviceLimits)
{
    auto invalid = meshRendererInfo();
    invalid.frameCount = 0;
    EXPECT_FALSE(panda::core::vulkan::renderer::MeshRenderer::create(invalid));
    invalid.frameCount = 1;
    invalid.extent.width = 0;
    EXPECT_FALSE(panda::core::vulkan::renderer::MeshRenderer::create(invalid));
    invalid.extent.width = 64;
    invalid.extent.height = 0;
    EXPECT_FALSE(panda::core::vulkan::renderer::MeshRenderer::create(invalid));
    invalid.extent.height = 64;
    invalid.colorFormat = vk::Format::eUndefined;
    EXPECT_FALSE(panda::core::vulkan::renderer::MeshRenderer::create(invalid));

    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxPushConstantsSize = 0;
            properties->limits.maxStorageBufferRange = 4096;
        });
    const auto unsupported = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(FrameBindingsTest, RejectsMeshRendererWithInsufficientStorageLimit)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxPushConstantsSize = std::numeric_limits<std::uint32_t>::max();
            properties->limits.maxStorageBufferRange = static_cast<std::uint32_t>(sizeof(MaterialRecord) - 1);
        });

    const auto unsupported = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());

    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(FrameBindingsTest, RejectsUnsupportedDepthFormats)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_,
                                                         VK_FORMAT_D32_SFLOAT,
                                                         VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         _,
                                                         0,
                                                         _))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorFormatNotSupported)));
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_,
                                                         VK_FORMAT_D16_UNORM,
                                                         VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         _,
                                                         0,
                                                         _))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorFormatNotSupported)));

    const auto renderer = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());

    ASSERT_FALSE(renderer);
    EXPECT_EQ(renderer.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(FrameBindingsTest, PropagatesMeshRendererDepthQueryAndShadowCreationFailures)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_,
                                                         VK_FORMAT_D32_SFLOAT,
                                                         VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         _,
                                                         0,
                                                         _))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));
    const auto queryFailed = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());
    ASSERT_FALSE(queryFailed);
    EXPECT_EQ(queryFailed.error().code, panda::ErrorCode::DeviceLost);

    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_,
                                                         VK_FORMAT_D32_SFLOAT,
                                                         VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         _,
                                                         0,
                                                         _))
        .WillOnce([](VkPhysicalDevice,
                     VkFormat,
                     VkImageType,
                     VkImageTiling,
                     VkImageUsageFlags,
                     VkImageCreateFlags,
                     VkImageFormatProperties* properties) {
            *properties = VkImageFormatProperties {};
            properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
            properties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(textures, create(Ref(device), Ref(*pool), _))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "shadow allocation failed")}}));
    const auto shadowFailed = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());
    ASSERT_FALSE(shadowFailed);
    EXPECT_EQ(shadowFailed.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, MeshRendererResizeFailureKeepsPreviousSampleCount)
{
    auto renderer = createMeshRenderer();
    ASSERT_TRUE(renderer);
    ASSERT_EQ((*renderer)->getSampleCount(), vk::SampleCountFlagBits::e4);
    EXPECT_TRUE((*renderer)->resize({.width = 64, .height = 64}, vk::Format::eR8G8B8A8Unorm));
    EXPECT_FALSE((*renderer)->resize({.width = 0, .height = 64}, vk::Format::eR8G8B8A8Unorm));

    EXPECT_CALL(textures, create(Ref(device), Ref(*pool), _))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "depth allocation failed")}}));
    const auto resized = (*renderer)->resize({.width = 128, .height = 64}, vk::Format::eR8G8B8A8Unorm);

    ASSERT_FALSE(resized);
    EXPECT_EQ(resized.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ((*renderer)->getSampleCount(), vk::SampleCountFlagBits::e4);
}

TEST_F(FrameBindingsTest, MeshRendererUpdatePropagatesBindingFailure)
{
    auto renderer = createMeshRenderer();
    ASSERT_TRUE(renderer);
    auto view = makeView(0);
    view.camera.viewProjection = glm::mat4 {0.0F};

    const auto updated = (*renderer)->updateFrame(0, view);

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(FrameBindingsTest, OneSampleColorPassClearsTheSwapchainWithoutResolveAttachment)
{
    auto renderer = createMeshRenderer(VK_SAMPLE_COUNT_1_BIT);
    ASSERT_TRUE(renderer);
    EXPECT_EQ((*renderer)->getSampleCount(), vk::SampleCountFlagBits::e1);

    auto* const swapchainImageView = fakeVulkanHandle<VkImageView>(900);
    auto colorPassCount = 0;
    EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull()))
        .Times(2)
        .WillRepeatedly([&](VkCommandBuffer, const VkRenderingInfo* rendering) {
            if (++colorPassCount == 2)
            {
                ASSERT_EQ(rendering->colorAttachmentCount, 1U);
                const auto& attachment = *rendering->pColorAttachments;
                EXPECT_EQ(attachment.imageView, swapchainImageView);
                EXPECT_EQ(attachment.loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
                EXPECT_EQ(attachment.storeOp, VK_ATTACHMENT_STORE_OP_STORE);
                EXPECT_EQ(attachment.resolveImageView, VK_NULL_HANDLE);
                EXPECT_EQ(attachment.resolveMode, VK_RESOLVE_MODE_NONE);
            }
        });
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull())).Times(AnyNumber());
    EXPECT_CALL(vulkan, vkCmdSetViewport(_, _, _, NotNull())).Times(2);
    EXPECT_CALL(vulkan, vkCmdSetScissor(_, _, _, NotNull())).Times(2);
    EXPECT_CALL(vulkan, vkCmdBindDescriptorSets(_)).Times(2);
    EXPECT_CALL(vulkan, vkCmdEndRendering(_)).Times(2);
    const auto command = vk::CommandBuffer {fakeVulkanHandle<VkCommandBuffer>(901)};
    const auto view = panda::RenderView {};

    (*renderer)->record(command, vk::ImageView {swapchainImageView}, 0, view);
}

TEST_F(FrameBindingsTest, PropagatesFrameBindingsInitializationFailure)
{
    const auto supportedLimits = [](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
        properties->limits.maxPushConstantsSize = 4096;
        properties->limits.maxStorageBufferRange = 4096;
        properties->limits.framebufferColorSampleCounts = allSampleCounts;
        properties->limits.framebufferDepthSampleCounts = allSampleCounts;
    };
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .Times(4)
        .WillOnce(supportedLimits)
        .WillOnce(supportedLimits)
        .WillOnce(supportedLimits)
        .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxStorageBufferRange = 0;
        });
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
        .Times(AnyNumber())
        .WillRepeatedly([](VkPhysicalDevice,
                           VkFormat,
                           VkImageType,
                           VkImageTiling,
                           VkImageUsageFlags,
                           VkImageCreateFlags,
                           VkImageFormatProperties* properties) {
            *properties = VkImageFormatProperties {};
            properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
            properties->sampleCounts = allSampleCounts;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(textures, create(Ref(device), Ref(*pool), _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(panda::Result<void> {}));

    const auto renderer = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());

    ASSERT_FALSE(renderer);
    EXPECT_EQ(renderer.error().code, panda::ErrorCode::Unsupported);
}

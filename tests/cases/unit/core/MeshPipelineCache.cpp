// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MeshPipelineCache.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <panda/RenderView.hpp>
#include <tuple>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"

namespace
{
using panda::core::vulkan::renderer::MeshPipelineCache;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;

class MeshPipelineCacheTest : public testing::Test
{
protected:
    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectShaderModules(std::size_t count)
    {
        EXPECT_CALL(vulkan, vkCreateShaderModule(deviceHandle, NotNull(), nullptr, NotNull()))
            .Times(static_cast<int>(count))
            .WillRepeatedly([this](VkDevice,
                                   const VkShaderModuleCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkShaderModule* output) {
                EXPECT_GT(info->codeSize, 0U);
                EXPECT_NE(info->pCode, nullptr);
                *output = fakeVulkanHandle<VkShaderModule>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyShaderModule(deviceHandle, _, nullptr)).Times(static_cast<int>(count));
    }

    void expectSuccessfulPipelines(std::size_t count)
    {
        EXPECT_CALL(vulkan, vkCreateGraphicsPipelines(deviceHandle, _, 1, NotNull(), nullptr, NotNull()))
            .Times(static_cast<int>(count))
            .WillRepeatedly([this](VkDevice,
                                   VkPipelineCache,
                                   std::uint32_t,
                                   const VkGraphicsPipelineCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkPipeline* output) {
                EXPECT_NE(info->pStages, nullptr);
                EXPECT_GT(info->stageCount, 0U);
                EXPECT_NE(info->pVertexInputState, nullptr);
                EXPECT_NE(info->pNext, nullptr);
                *output = fakeVulkanHandle<VkPipeline>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyPipeline(deviceHandle, _, nullptr)).Times(static_cast<int>(count));
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeVulkanHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::uintptr_t nextHandle {10};
};
}

TEST_F(MeshPipelineCacheTest, MeshKeysSeparateLayoutAndPipelineState)
{
    const auto baseMeshKey = MeshPipelineCache::MeshPipelineKey {.vertexStride = 32, .positionOffset = 0};
    auto otherMeshKey = baseMeshKey;
    otherMeshKey.vertexStride = 36;
    EXPECT_NE(baseMeshKey, otherMeshKey);
    otherMeshKey = baseMeshKey;
    otherMeshKey.positionOffset = 4;
    EXPECT_NE(baseMeshKey, otherMeshKey);
    otherMeshKey = baseMeshKey;
    otherMeshKey.alphaMode = panda::AlphaMode::Blend;
    EXPECT_NE(baseMeshKey, otherMeshKey);
    otherMeshKey = baseMeshKey;
    otherMeshKey.renderFace = panda::RenderFace::Both;
    EXPECT_NE(baseMeshKey, otherMeshKey);
    otherMeshKey = baseMeshKey;
    otherMeshKey.mirrored = true;
    EXPECT_NE(baseMeshKey, otherMeshKey);
}

TEST_F(MeshPipelineCacheTest, MaterialValuesOutsidePipelineStateDoNotChangeKeys)
{
    auto draw = panda::MeshDraw {};
    const auto meshKey = MeshPipelineCache::makeMeshPipelineKey(draw);
    const auto shadowKey = MeshPipelineCache::makeShadowPipelineKey(draw);
    draw.tint = {0.1F, 0.4F, 0.8F, 0.5F};
    draw.material.baseColor = {0.8F, 0.2F, 0.6F, 0.7F};
    draw.material.metallic = 0.7F;
    draw.material.roughness = 0.2F;
    draw.material.emissive = {0.2F, 0.3F, 0.5F};
    draw.material.alphaCutoff = 0.9F;
    EXPECT_EQ(meshKey, MeshPipelineCache::makeMeshPipelineKey(draw));
    EXPECT_EQ(shadowKey, MeshPipelineCache::makeShadowPipelineKey(draw));
}

TEST_F(MeshPipelineCacheTest, MeshKeyTracksModelAndMirroredWinding)
{
    auto draw = panda::MeshDraw {};
    const auto litKey = MeshPipelineCache::makeMeshPipelineKey(draw);
    draw.material.model = panda::MaterialModel::Unlit;
    EXPECT_NE(litKey, MeshPipelineCache::makeMeshPipelineKey(draw));
    draw.material.model = panda::MaterialModel::Lit;
    draw.modelMatrix = glm::scale(glm::mat4 {1.0F}, glm::vec3 {-1.0F, 1.0F, 1.0F});
    EXPECT_NE(litKey, MeshPipelineCache::makeMeshPipelineKey(draw));
}

TEST_F(MeshPipelineCacheTest, ShadowKeyDistinguishesTexturedMaskVariant)
{
    const auto texturedMaskShadow = MeshPipelineCache::ShadowPipelineKey {.vertexStride = 32,
                                                                          .positionOffset = 0,
                                                                          .texCoordOffset = 16,
                                                                          .hasBaseColorTexture = true,
                                                                          .alphaMode = panda::AlphaMode::Mask};
    auto untexturedMaskShadow = texturedMaskShadow;
    untexturedMaskShadow.hasBaseColorTexture = false;
    untexturedMaskShadow.texCoordOffset = 0;
    EXPECT_NE(texturedMaskShadow, untexturedMaskShadow);
}

TEST_F(MeshPipelineCacheTest, PreparesSeparateTexturedAndUntexturedMaskShadowVariants)
{
    expectShaderModules(4);
    expectSuccessfulPipelines(2);
    auto cache = MeshPipelineCache {device, vk::PipelineLayout {fakeVulkanHandle<VkPipelineLayout>(90)}};
    cache.setOutputCompatibility({.colorFormat = vk::Format::eB8G8R8A8Srgb,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e1});
    const auto texturedMaskShadow = MeshPipelineCache::ShadowPipelineKey {.vertexStride = 32,
                                                                          .positionOffset = 0,
                                                                          .texCoordOffset = 16,
                                                                          .hasBaseColorTexture = true,
                                                                          .alphaMode = panda::AlphaMode::Mask};
    auto untexturedMaskShadow = texturedMaskShadow;
    untexturedMaskShadow.hasBaseColorTexture = false;
    untexturedMaskShadow.texCoordOffset = 0;

    ASSERT_TRUE(cache.prepare(texturedMaskShadow));
    ASSERT_TRUE(cache.prepare(untexturedMaskShadow));
    ASSERT_TRUE(cache.prepare(texturedMaskShadow));
}

TEST_F(MeshPipelineCacheTest, PrepareCachesVariantsAndBlendDoesNotCreateShadowPipeline)
{
    expectShaderModules(2);
    expectSuccessfulPipelines(1);

    auto cache = MeshPipelineCache {device, vk::PipelineLayout {fakeVulkanHandle<VkPipelineLayout>(90)}};
    cache.setOutputCompatibility({.colorFormat = vk::Format::eB8G8R8A8Srgb,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e1});
    auto view = panda::RenderView {};
    view.meshes.emplace_back();
    view.meshes.back().material.model = panda::MaterialModel::Unlit;
    view.meshes.back().material.alphaMode = panda::AlphaMode::Blend;
    ASSERT_TRUE(cache.prepare(view));
    ASSERT_TRUE(cache.prepare(view));
    const auto pipeline = cache.getPipeline(view.meshes.front());
    EXPECT_NE(pipeline, vk::Pipeline {});
    EXPECT_EQ(cache.getPipeline(view.meshes.front()), pipeline);
}

TEST_F(MeshPipelineCacheTest, FailedCreationCanBeRetriedWithoutPublishingAnInvalidEntry)
{
    expectShaderModules(4);
    EXPECT_CALL(vulkan, vkCreateGraphicsPipelines(deviceHandle, _, 1, NotNull(), nullptr, NotNull()))
        .Times(2)
        .WillOnce([](VkDevice,
                     VkPipelineCache,
                     std::uint32_t,
                     const VkGraphicsPipelineCreateInfo*,
                     const VkAllocationCallbacks*,
                     VkPipeline*) {
            return static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory);
        })
        .WillOnce([this](VkDevice,
                         VkPipelineCache,
                         std::uint32_t,
                         const VkGraphicsPipelineCreateInfo*,
                         const VkAllocationCallbacks*,
                         VkPipeline* output) {
            *output = fakeVulkanHandle<VkPipeline>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkDestroyPipeline(deviceHandle, _, nullptr));

    auto cache = MeshPipelineCache {device, vk::PipelineLayout {fakeVulkanHandle<VkPipelineLayout>(90)}};
    cache.setOutputCompatibility({.colorFormat = vk::Format::eB8G8R8A8Srgb,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e1});
    const auto key = MeshPipelineCache::MeshPipelineKey {.lit = false, .vertexStride = 12};
    EXPECT_FALSE(cache.prepare(key));
    EXPECT_TRUE(cache.prepare(key));
}

TEST_F(MeshPipelineCacheTest, OutputCompatibilityInvalidatesOnlyDependentVariants)
{
    expectShaderModules(10);
    expectSuccessfulPipelines(6);
    auto cache = MeshPipelineCache {device, vk::PipelineLayout {fakeVulkanHandle<VkPipelineLayout>(90)}};
    cache.setOutputCompatibility({.colorFormat = vk::Format::eB8G8R8A8Srgb,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e1});
    const auto meshKey = MeshPipelineCache::MeshPipelineKey {.lit = false, .vertexStride = 12};
    const auto shadowKey = MeshPipelineCache::ShadowPipelineKey {.vertexStride = 12};

    ASSERT_TRUE(cache.prepare(meshKey));
    ASSERT_TRUE(cache.prepare(shadowKey));
    cache.setOutputCompatibility({.colorFormat = vk::Format::eR8G8B8A8Unorm,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e1});
    ASSERT_TRUE(cache.prepare(meshKey));
    ASSERT_TRUE(cache.prepare(shadowKey));
    cache.setOutputCompatibility({.colorFormat = vk::Format::eR8G8B8A8Unorm,
                                  .depthFormat = vk::Format::eD32Sfloat,
                                  .sampleCount = vk::SampleCountFlagBits::e2});
    ASSERT_TRUE(cache.prepare(meshKey));
    cache.setOutputCompatibility({.colorFormat = vk::Format::eR8G8B8A8Unorm,
                                  .depthFormat = vk::Format::eD16Unorm,
                                  .sampleCount = vk::SampleCountFlagBits::e2});
    ASSERT_TRUE(cache.prepare(meshKey));
    ASSERT_TRUE(cache.prepare(shadowKey));
}

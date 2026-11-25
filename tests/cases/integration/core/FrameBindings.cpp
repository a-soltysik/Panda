// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBindings.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "FakeHandle.hpp"
#include "Math.hpp"
#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "presentation/FrameExecution.hpp"
#include "presentation/ToneMappingPass.hpp"
#include "renderer/DescriptorSets.hpp"
#include "renderer/FrameBuffers.hpp"
#include "renderer/MaterialTextureBindings.hpp"
#include "renderer/MeshRenderer.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
constexpr auto allSampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                 static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                 static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);

using panda::core::vulkan::presentation::FrameExecution;
using panda::core::vulkan::renderer::FrameBindings;
using panda::core::vulkan::renderer::FrameBuffers;
using panda::core::vulkan::renderer::LightRecord;
using panda::core::vulkan::renderer::MaterialRecord;
using panda::core::vulkan::renderer::MaterialTextureBindings;
using panda::core::vulkan::renderer::MeshInstanceData;
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::BufferResource;
using panda::test::fakeHandle;
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
        initializeMemoryPool();
        installDeviceProperties();
        installFormatSupport();
        installDescriptorDefaults();
        installToneMappingPipelineLayoutDefault();
        EXPECT_CALL(buffers, createNative(_, Ref(*pool), _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(panda::Result<void> {}));
        EXPECT_CALL(buffers, getNative(_)).Times(AnyNumber()).WillRepeatedly([this](const BufferResource&) {
            return fakeHandle<VkBuffer>(nextHandle++);
        });
        EXPECT_CALL(buffers, writeMapped(_, _, _)).Times(AnyNumber()).WillRepeatedly(Return(panda::Result<void> {}));
    }

    void initializeMemoryPool()
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
    }

    void installDeviceProperties()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
                properties->limits.maxPushConstantsSize = 4096;
                properties->limits.maxStorageBufferRange = 4096;
                properties->limits.maxDescriptorSetStorageBuffers = 4;
                properties->limits.maxPerStageDescriptorStorageBuffers = 4;
                properties->limits.maxBoundDescriptorSets = 4;
                properties->limits.minUniformBufferOffsetAlignment = 256;
                properties->limits.framebufferColorSampleCounts = allSampleCounts;
                properties->limits.framebufferDepthSampleCounts = allSampleCounts;
                properties->limits.maxSamplerAnisotropy = maxSamplerAnisotropy;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFeatures2(_, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
                features->features.samplerAnisotropy = samplerAnisotropy;
            });
    }

    void installFormatSupport()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFormatProperties(_, VK_FORMAT_R16G16B16A16_SFLOAT, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
                properties->optimalTilingFeatures =
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) |
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) |
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
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
                properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                properties->sampleCounts = allSampleCounts;
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void TearDown() override
    {
        queue.reset();
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto createBindings(std::uint32_t maxLights = panda::EngineOptions::defaultMaxLights)
        -> std::expected<std::unique_ptr<FrameBindings>, panda::Error>
    {
        return FrameBindings::create({.physicalDevice = physical,
                                      .device = device,
                                      .pool = *pool,
                                      .frameCount = 1,
                                      .maxLights = maxLights,
                                      .textureFilteringPreset = nullptr});
    }

    auto meshRendererInfo() const -> panda::core::vulkan::renderer::MeshRendererCreateInfo
    {
        return {
            .physicalDevice = physical,
            .device = device,
            .pool = *pool,
            .extent = {.width = 64, .height = 64},
            .colorFormat = vk::Format::eR8G8B8A8Unorm,
            .frameCount = 1,
            .maxLights = panda::EngineOptions::defaultMaxLights,
            .textureFilteringPreset = nullptr
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
        expectMeshRendererTextures();
        return panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());
    }

    void expectMeshRendererTextures()
    {
        EXPECT_CALL(textures, createNative(Ref(device), Ref(*pool), _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(panda::Result<void> {}));
        EXPECT_CALL(textures, getNative(_)).Times(AnyNumber()).WillRepeatedly(Return(fakeHandle<VkImage>(91)));
        EXPECT_CALL(textures, getNativeView(_)).Times(AnyNumber()).WillRepeatedly(Return(fakeHandle<VkImageView>(90)));
    }

    auto createFrameExecution() -> std::expected<std::unique_ptr<FrameExecution>, panda::Error>
    {
        EXPECT_CALL(submissions, construct(_, 0));
        queue.emplace(device, 0);
        EXPECT_CALL(vulkan, vkCreateCommandPool(deviceHandle, NotNull(), _, NotNull()))
            .Times(2)
            .WillRepeatedly(
                [this](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* output) {
                    *output = fakeHandle<VkCommandPool>(nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(deviceHandle, NotNull(), NotNull()))
            .Times(2)
            .WillRepeatedly([this](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
                for (auto& command : std::span {output, info->commandBufferCount})
                {
                    command = fakeHandle<VkCommandBuffer>(nextHandle++);
                }
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyCommandPool(deviceHandle, _, _)).Times(2);
        EXPECT_CALL(submissions, waitValue(0, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(panda::Result<panda::WaitStatus> {panda::WaitStatus::Completed}));
        return FrameExecution::create({.physicalDevice = physical,
                                       .device = device,
                                       .queueFamily = 0,
                                       .submissions = *queue,
                                       .pool = pool.get(),
                                       .performance = nullptr,
                                       .textureFilteringPreset = nullptr,
                                       .maxLights = panda::EngineOptions::defaultMaxLights});
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

    void installToneMappingPipelineLayoutDefault()
    {
        EXPECT_CALL(vulkan,
                    vkCreatePipelineLayout(_,
                                           testing::Truly([](const VkPipelineLayoutCreateInfo* info) {
                                               return info->setLayoutCount == 1U;
                                           }),
                                           _,
                                           NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkPipelineLayoutCreateInfo*,
                                   const VkAllocationCallbacks*,
                                   VkPipelineLayout* output) {
                *output = fakeHandle<VkPipelineLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    auto createToneMappingPass(std::uint32_t maxBoundDescriptorSets = 4)
    {
        return panda::core::vulkan::presentation::ToneMappingPass::create(device, maxBoundDescriptorSets);
    }

    void expectToneMappingShaders(std::size_t count)
    {
        EXPECT_CALL(vulkan, vkCreateShaderModule(deviceHandle, NotNull(), nullptr, NotNull()))
            .Times(static_cast<int>(count))
            .WillRepeatedly([this](VkDevice,
                                   const VkShaderModuleCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkShaderModule* output) {
                EXPECT_GT(info->codeSize, 0U);
                EXPECT_NE(info->pCode, nullptr);
                *output = fakeHandle<VkShaderModule>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyShaderModule(deviceHandle, _, nullptr)).Times(static_cast<int>(count));
    }

    void expectToneMappingPipelines(std::size_t count, VkResult result)
    {
        EXPECT_CALL(vulkan, vkCreateGraphicsPipelines(deviceHandle, _, 1, NotNull(), nullptr, NotNull()))
            .Times(static_cast<int>(count))
            .WillRepeatedly([this, result](VkDevice,
                                           VkPipelineCache,
                                           std::uint32_t,
                                           const VkGraphicsPipelineCreateInfo* info,
                                           const VkAllocationCallbacks*,
                                           VkPipeline* output) {
                EXPECT_EQ(info->stageCount, 2U);
                EXPECT_NE(info->pNext, nullptr);
                if (result == VK_SUCCESS)
                {
                    *output = fakeHandle<VkPipeline>(nextHandle++);
                }
                return result;
            });
        if (result == VK_SUCCESS)
        {
            EXPECT_CALL(vulkan, vkDestroyPipeline(deviceHandle, _, nullptr)).Times(static_cast<int>(count));
        }
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
                *output = fakeHandle<VkDescriptorSetLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkCreatePipelineLayout(_, NotNull(), _, NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice,
                                   const VkPipelineLayoutCreateInfo* info,
                                   const VkAllocationCallbacks*,
                                   VkPipelineLayout* output) {
                EXPECT_EQ(info->setLayoutCount, 2U);
                *output = fakeHandle<VkPipelineLayout>(nextHandle++);
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
                *output = fakeHandle<VkDescriptorPool>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkAllocateDescriptorSets(_, NotNull(), NotNull()))
            .Times(AnyNumber())
            .WillRepeatedly([this](VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
                for (auto& descriptorSet : std::span {output, info->descriptorSetCount})
                {
                    descriptorSet = fakeHandle<VkDescriptorSet>(nextHandle++);
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
                    *output = fakeHandle<VkSampler>(nextHandle++);
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
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissions;
    panda::test::ScopedMock<panda::test::TextureResourceMock> textures;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
    std::optional<panda::detail::SubmissionQueue> queue;
    std::uintptr_t nextHandle {10};
    VkBool32 samplerAnisotropy {VK_FALSE};
    float maxSamplerAnisotropy {6.0F};
};
}

TEST(MeshRenderer, EncodesLinearClearForTheAttachmentFormat)
{
    const auto srgb = panda::core::vulkan::renderer::makeClearColor(vk::Format::eB8G8R8A8Srgb);
    const auto redSrgb = panda::core::vulkan::renderer::makeClearColor(vk::Format::eR8G8B8A8Srgb);
    const auto unorm = panda::core::vulkan::renderer::makeClearColor(vk::Format::eB8G8R8A8Unorm);
    EXPECT_NEAR(srgb.float32.at(0), 0.025F, 0.0001F);
    EXPECT_NEAR(redSrgb.float32.at(0), 0.025F, 0.0001F);
    EXPECT_NEAR(unorm.float32.at(0), 0.1718F, 0.0001F);
    EXPECT_FLOAT_EQ(srgb.float32.at(3), 1.0F);
    EXPECT_FLOAT_EQ(unorm.float32.at(3), 1.0F);
}

TEST(MeshRenderer, EncodesToneMappedClearForSrgbAndUnormOutputs)
{
    const auto blueSrgb = panda::core::vulkan::renderer::makeToneMappedClearColor(vk::Format::eB8G8R8A8Srgb);
    const auto redSrgb = panda::core::vulkan::renderer::makeToneMappedClearColor(vk::Format::eR8G8B8A8Srgb);
    const auto unorm = panda::core::vulkan::renderer::makeToneMappedClearColor(vk::Format::eB8G8R8A8Unorm);
    EXPECT_FLOAT_EQ(blueSrgb.float32.at(0), redSrgb.float32.at(0));
    EXPECT_NEAR(unorm.float32.at(0), ::panda::detail::linearToSrgb(blueSrgb.float32.at(0)), 0.0001F);
    EXPECT_FLOAT_EQ(blueSrgb.float32.at(3), 1.0F);
    EXPECT_FLOAT_EQ(unorm.float32.at(3), 1.0F);
}

TEST_F(FrameBindingsTest, ToneMappingInitializationPropagatesVulkanFailures)
{
    constexpr auto failure = static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory);

    EXPECT_CALL(vulkan,
                vkCreateSampler(deviceHandle,
                                testing::Truly([](const VkSamplerCreateInfo* info) {
                                    return info->magFilter == VK_FILTER_NEAREST &&
                                           info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                                           info->borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                                }),
                                _,
                                NotNull()))
        .WillOnce(Return(failure))
        .RetiresOnSaturation();
    auto pass = createToneMappingPass();
    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);

    EXPECT_CALL(vulkan,
                vkCreateDescriptorSetLayout(deviceHandle,
                                            testing::Truly([](const VkDescriptorSetLayoutCreateInfo* info) {
                                                return info->bindingCount == 1U;
                                            }),
                                            _,
                                            NotNull()))
        .WillOnce(Return(failure))
        .RetiresOnSaturation();
    pass = createToneMappingPass();
    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);

    EXPECT_CALL(vulkan,
                vkCreatePipelineLayout(deviceHandle,
                                       testing::Truly([](const VkPipelineLayoutCreateInfo* info) {
                                           return info->setLayoutCount == 1U;
                                       }),
                                       _,
                                       NotNull()))
        .WillOnce(Return(failure))
        .RetiresOnSaturation();
    pass = createToneMappingPass();
    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);

    EXPECT_CALL(vulkan,
                vkCreateDescriptorPool(deviceHandle,
                                       testing::Truly([](const VkDescriptorPoolCreateInfo* info) {
                                           return info->maxSets == 2U;
                                       }),
                                       _,
                                       NotNull()))
        .WillOnce(Return(failure))
        .RetiresOnSaturation();
    pass = createToneMappingPass();
    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);

    EXPECT_CALL(vulkan,
                vkAllocateDescriptorSets(deviceHandle,
                                         testing::Truly([](const VkDescriptorSetAllocateInfo* info) {
                                             return info->descriptorSetCount == 2U;
                                         }),
                                         NotNull()))
        .WillOnce(Return(failure))
        .RetiresOnSaturation();
    pass = createToneMappingPass();
    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, ToneMappingRejectsDescriptorSetBeforeCreatingLayouts)
{
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    const auto pass = createToneMappingPass(0);

    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(FrameBindingsTest, ToneMappingPipelinePropagatesShaderAndPipelineFailures)
{
    auto pass = createToneMappingPass();
    ASSERT_TRUE(pass);
    EXPECT_CALL(vulkan, vkCreateShaderModule(deviceHandle, NotNull(), nullptr, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));
    auto prepared = (*pass)->prepare(vk::Format::eB8G8R8A8Srgb);
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::DeviceLost);

    pass = createToneMappingPass();
    ASSERT_TRUE(pass);
    EXPECT_CALL(vulkan, vkCreateShaderModule(deviceHandle, NotNull(), nullptr, NotNull()))
        .WillOnce(
            [this](VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* output) {
                *output = fakeHandle<VkShaderModule>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)));
    EXPECT_CALL(vulkan, vkDestroyShaderModule(deviceHandle, _, nullptr));
    prepared = (*pass)->prepare(vk::Format::eR8G8B8A8Srgb);
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::DeviceLost);

    pass = createToneMappingPass();
    ASSERT_TRUE(pass);
    expectToneMappingShaders(2);
    expectToneMappingPipelines(1, static_cast<VkResult>(vk::Result::eErrorDeviceLost));
    prepared = (*pass)->prepare(vk::Format::eB8G8R8A8Unorm);
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(FrameBindingsTest, ToneMappingSetsInputsAndCachesSrgbPipelineVariants)
{
    auto pass = createToneMappingPass();
    ASSERT_TRUE(pass);
    const auto first = vk::ImageView {fakeHandle<VkImageView>(1001)};
    const auto second = vk::ImageView {fakeHandle<VkImageView>(1002)};
    auto inputs = (*pass)->setInputViews(std::array<vk::ImageView, 2> {first, vk::ImageView {}});
    ASSERT_FALSE(inputs);
    EXPECT_EQ(inputs.error().code, panda::ErrorCode::InvalidArgument);
    inputs = (*pass)->setInputViews(std::array<vk::ImageView, 2> {vk::ImageView {}, second});
    ASSERT_FALSE(inputs);
    EXPECT_EQ(inputs.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE((*pass)->setInputViews(std::array<vk::ImageView, 2> {first, second}));

    expectToneMappingShaders(6);
    EXPECT_CALL(vulkan, vkCreateGraphicsPipelines(deviceHandle, _, 1, NotNull(), nullptr, NotNull()))
        .Times(3)
        .WillRepeatedly([this](VkDevice,
                               VkPipelineCache,
                               std::uint32_t,
                               const VkGraphicsPipelineCreateInfo* info,
                               const VkAllocationCallbacks*,
                               VkPipeline* output) {
            const auto* const rendering = static_cast<const VkPipelineRenderingCreateInfo*>(info->pNext);
            EXPECT_EQ(rendering->colorAttachmentCount, 1U);
            const auto stages = std::span {info->pStages, info->stageCount};
            const auto* const specialization = stages.back().pSpecializationInfo;
            const auto colorFormat =
                std::span {rendering->pColorAttachmentFormats, rendering->colorAttachmentCount}.front();
            EXPECT_NE(specialization, nullptr);
            if (specialization == nullptr)
            {
                return static_cast<VkResult>(vk::Result::eErrorUnknown);
            }
            const auto* const isSrgb = static_cast<const std::uint32_t*>(specialization->pData);
            EXPECT_EQ(*isSrgb, colorFormat == VK_FORMAT_B8G8R8A8_SRGB || colorFormat == VK_FORMAT_R8G8B8A8_SRGB);
            *output = fakeHandle<VkPipeline>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkDestroyPipeline(deviceHandle, _, nullptr)).Times(3);

    ASSERT_TRUE((*pass)->prepare(vk::Format::eB8G8R8A8Srgb));
    ASSERT_TRUE((*pass)->prepare(vk::Format::eR8G8B8A8Srgb));
    ASSERT_TRUE((*pass)->prepare(vk::Format::eB8G8R8A8Unorm));
    EXPECT_TRUE((*pass)->prepare(vk::Format::eB8G8R8A8Srgb));
}

TEST_F(FrameBindingsTest, FrameExecutionPropagatesToneMappingInitializationFailure)
{
    expectMeshRendererTextures();
    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    const auto view = makeView(1);
    EXPECT_CALL(vulkan,
                vkCreateSampler(deviceHandle,
                                testing::Truly([](const VkSamplerCreateInfo* info) {
                                    return info->magFilter == VK_FILTER_NEAREST &&
                                           info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                                           info->borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                                }),
                                _,
                                NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)))
        .RetiresOnSaturation();

    const auto prepared = (*execution)->prepareOutput({.width = 64, .height = 64}, vk::Format::eB8G8R8A8Srgb, view);

    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::ResourceExhausted);

    expectToneMappingShaders(2);
    expectToneMappingPipelines(1, static_cast<VkResult>(vk::Result::eSuccess));
    const auto retried = (*execution)->prepareOutput({.width = 128, .height = 64}, vk::Format::eB8G8R8A8Srgb, view);
    ASSERT_TRUE(retried);
}

TEST_F(FrameBindingsTest, FrameExecutionPropagatesToneMappingPipelineFailure)
{
    expectMeshRendererTextures();
    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    const auto view = makeView(1);
    EXPECT_CALL(vulkan, vkCreateShaderModule(deviceHandle, NotNull(), nullptr, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorDeviceLost)))
        .RetiresOnSaturation();

    const auto prepared = (*execution)->prepareOutput({.width = 64, .height = 64}, vk::Format::eB8G8R8A8Srgb, view);

    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(FrameBindingsTest, FrameExecutionPropagatesResizeAndRecordingFailures)
{
    expectMeshRendererTextures();
    auto execution = createFrameExecution();
    ASSERT_TRUE(execution);
    const auto view = makeView(1);
    expectToneMappingShaders(2);
    expectToneMappingPipelines(1, static_cast<VkResult>(vk::Result::eSuccess));
    ASSERT_TRUE(
        (*execution)
            ->prepareOutput({.width = 64, .height = 64}, vk::Format::eB8G8R8A8Srgb, view, vk::Format::eB8G8R8A8Srgb));
    ASSERT_TRUE((*execution)->prepareOutput({.width = 128, .height = 64}, vk::Format::eB8G8R8A8Srgb, view));

    (*execution)->setLastCompletion(0, 1);
    EXPECT_CALL(submissions, waitValue(1, 10'000'000'000))
        .WillOnce(Return(panda::Result<panda::WaitStatus> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "wait failed")}}));
    const auto resized = (*execution)->prepareOutput({.width = 192, .height = 64}, vk::Format::eB8G8R8A8Srgb, view);
    ASSERT_FALSE(resized);
    EXPECT_EQ(resized.error().code, panda::ErrorCode::ResourceExhausted);

    (*execution)->setLastCompletion(0, 0);
    ASSERT_TRUE((*execution)->prepareSlot(0, view));
    EXPECT_CALL(buffers, writeMapped(_, _, _)).WillOnce(Return(bufferFailure())).RetiresOnSaturation();
    const auto recorded = (*execution)
                              ->recordContent(0,
                                              {
                                                  .imageView = fakeHandle<VkImageView>(900),
                                                  .extent = {.width = 128, .height = 64},
                                                  .colorFormat = vk::Format::eB8G8R8A8Srgb
    },
                                              view);
    ASSERT_FALSE(recorded);
    EXPECT_EQ(recorded.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, PacksMaterialValuesAndWritesFallbackTextureDescriptors)
{
    auto created = createBindings();
    ASSERT_TRUE(created) << created.error().message;
    auto bindings = std::move(*created);
    auto view = makeView(1);
    auto& material = view.meshes.front().material;
    material.model = panda::MaterialModel::Unlit;
    material.renderFace = panda::RenderFace::Both;
    material.metallic = 1.5F;
    material.roughness = -0.5F;
    material.alphaCutoff = 0.4F;
    material.emissive = {0.2F, 0.4F, 0.8F};
    material.normalScale = 3.0F;
    material.occlusionStrength = 1.5F;
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(3)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });
    const auto shadowView = vk::ImageView {fakeHandle<VkImageView>(90)};
    EXPECT_CALL(vulkan, vkUpdateDescriptorSets(deviceHandle, 6, NotNull(), 0, nullptr))
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
        });
    EXPECT_CALL(vulkan, vkUpdateDescriptorSets(deviceHandle, 5, NotNull(), 0, nullptr))
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
    ASSERT_EQ(writes.size(), 3U);
    ASSERT_EQ(writes.at(1).size(), sizeof(MaterialRecord));
    ASSERT_EQ(writes.at(2).size(), sizeof(MeshInstanceData));
    auto record = MaterialRecord {};
    std::memcpy(&record, writes.at(1).data(), sizeof(record));
    EXPECT_FLOAT_EQ(record.metallic, 1.0F);
    EXPECT_FLOAT_EQ(record.roughness, 0.0F);
    EXPECT_EQ(record.isLit, 0U);
    EXPECT_EQ(record.renderFace, static_cast<std::uint32_t>(panda::RenderFace::Both));
    EXPECT_FLOAT_EQ(record.alphaCutoff, 0.4F);
    EXPECT_EQ(record.hasBaseColorTexture, 0U);
    EXPECT_EQ(record.hasMetallicRoughnessTexture, 0U);
    EXPECT_EQ(record.hasEmissiveTexture, 0U);
    EXPECT_EQ(record.hasOcclusionTexture, 0U);
    EXPECT_FLOAT_EQ(record.occlusionStrength, 1.0F);
    EXPECT_FLOAT_EQ(record.emissiveAndNormalScale.x, 0.2F);
    EXPECT_FLOAT_EQ(record.emissiveAndNormalScale.y, 0.4F);
    EXPECT_FLOAT_EQ(record.emissiveAndNormalScale.z, 0.8F);
    EXPECT_FLOAT_EQ(record.emissiveAndNormalScale.w, 3.0F);
    auto instanceRecord = MeshInstanceData {};
    std::memcpy(&instanceRecord, writes.at(2).data(), sizeof(instanceRecord));
    EXPECT_EQ(instanceRecord.materialIndex, 0U);
}

TEST_F(FrameBindingsTest, CapsAnisotropyAndFallsBackWhenTheFeatureIsUnavailable)
{
    auto unsupported = createBindings();
    ASSERT_TRUE(unsupported);
    EXPECT_FLOAT_EQ((*unsupported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Anisotropic8x), 1.0F);

    samplerAnisotropy = VK_TRUE;
    auto supported = createBindings();
    ASSERT_TRUE(supported);
    EXPECT_FLOAT_EQ((*supported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Trilinear), 1.0F);
    EXPECT_FLOAT_EQ((*supported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Anisotropic2x), 2.0F);
    EXPECT_FLOAT_EQ((*supported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Anisotropic4x), 4.0F);
    EXPECT_FLOAT_EQ((*supported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Anisotropic8x), 6.0F);
    EXPECT_FLOAT_EQ((*supported)->getEffectiveAnisotropy(panda::TextureFilteringPreset::Anisotropic16x), 6.0F);
}

TEST_F(FrameBindingsTest, MaterialBufferGrowthFailureCanBeRetried)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    const auto largerBufferSize = sizeof(MaterialRecord) * 2;
    EXPECT_CALL(buffers,
                createNative(Ref(device),
                             Ref(*pool),
                             testing::Field(&panda::detail::BufferResourceCreateInfo::size, largerBufferSize)))
        .Times(2)
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "buffer growth failed")}}))
        .WillOnce(Return(panda::Result<void> {}));
    auto view = makeView(2);
    const auto shadowView = vk::ImageView {fakeHandle<VkImageView>(90)};

    const auto failed = bindings->updateFrame(0, view, shadowView);
    const auto retried = bindings->updateFrame(0, view, shadowView);

    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(retried);
}

TEST_F(FrameBindingsTest, WritesAllDirectionalLightsToTheStorageBuffer)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    auto view = makeView(1);
    view.lights = {
        panda::DirectionalLight {.direction = {0.0F, -2.0F, 0.0F},
                                 .color = {1.0F, 0.5F, 0.25F},
                                 .intensity = 3.0F,
                                 .shadow = {}},
        panda::DirectionalLight {.direction = {0.0F, 0.0F, -4.0F},
                                 .color = {0.25F, 0.5F, 1.0F},
                                 .intensity = 2.0F,
                                 .shadow = {}}
    };
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(4)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });

    const auto updated = bindings->updateFrame(0, view, {});

    ASSERT_TRUE(updated);
    ASSERT_EQ(writes.size(), 4U);
    ASSERT_EQ(writes.at(0).size(), sizeof(panda::core::vulkan::renderer::FrameUniform));
    ASSERT_EQ(writes.at(3).size(), sizeof(LightRecord) * 2U);
    auto uniform = panda::core::vulkan::renderer::FrameUniform {};
    std::memcpy(&uniform, writes.at(0).data(), sizeof(uniform));
    EXPECT_EQ(uniform.lightInfo.x, 2U);
    auto records = std::array<LightRecord, 2> {};
    std::memcpy(records.data(), writes.at(3).data(), writes.at(3).size());
    EXPECT_FLOAT_EQ(records.at(0).directionOuterConeCos.y, -1.0F);
    EXPECT_FLOAT_EQ(records.at(0).colorIntensity.y, 0.5F);
    EXPECT_FLOAT_EQ(records.at(0).colorIntensity.w, 3.0F);
    EXPECT_FLOAT_EQ(records.at(1).directionOuterConeCos.z, -1.0F);
    EXPECT_FLOAT_EQ(records.at(1).colorIntensity.x, 0.25F);
    EXPECT_FLOAT_EQ(records.at(1).colorIntensity.w, 2.0F);
    EXPECT_EQ(records.at(0).type,
              static_cast<std::uint32_t>(panda::core::vulkan::renderer::ShaderLightType::Directional));
}

TEST_F(FrameBindingsTest, PacksPointAndSpotLightsWithoutImplicitShadows)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    auto view = makeView(1);
    view.lights = {
        panda::PointLight {.position = {1.0F, 2.0F, 3.0F},
                           .color = {0.2F, 0.3F, 0.4F},
                           .intensity = 5.0F,
                           .range = 8.0F,
                           .shadow = {}},
        panda::SpotLight {.position = {-1.0F, 0.0F, 2.0F},
                           .direction = {0.0F, -2.0F, 0.0F},
                           .color = {0.5F, 0.6F, 0.7F},
                           .intensity = 6.0F,
                           .range = 10.0F,
                           .innerConeAngleRadians = 0.2F,
                           .outerConeAngleRadians = 0.6F,
                           .shadow = {}},
        panda::DirectionalLight {.direction = {0.0F, -1.0F, 0.0F}, .shadow = {}}
    };
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(4)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });

    const auto updated = bindings->updateFrame(0, view, {});

    ASSERT_TRUE(updated);
    ASSERT_EQ(writes.size(), 4U);
    auto uniform = panda::core::vulkan::renderer::FrameUniform {};
    std::memcpy(&uniform, writes.at(0).data(), sizeof(uniform));
    EXPECT_EQ(uniform.lightInfo.y, 0U);
    ASSERT_EQ(writes.at(3).size(), sizeof(LightRecord) * 3U);
    auto records = std::array<LightRecord, 3> {};
    std::memcpy(records.data(), writes.at(3).data(), writes.at(3).size());
    EXPECT_EQ(records.at(0).type, static_cast<std::uint32_t>(panda::core::vulkan::renderer::ShaderLightType::Point));
    EXPECT_EQ(records.at(0).positionRange, glm::vec4(1.0F, 2.0F, 3.0F, 8.0F));
    EXPECT_FLOAT_EQ(records.at(0).colorIntensity.w, 5.0F);
    EXPECT_EQ(records.at(1).type, static_cast<std::uint32_t>(panda::core::vulkan::renderer::ShaderLightType::Spot));
    EXPECT_FLOAT_EQ(records.at(1).directionOuterConeCos.y, -1.0F);
    EXPECT_FLOAT_EQ(records.at(1).positionRange.w, 10.0F);
    EXPECT_FLOAT_EQ(records.at(1).innerConeCos, std::cos(0.2F));
    EXPECT_FLOAT_EQ(records.at(1).directionOuterConeCos.w, std::cos(0.6F));
    EXPECT_EQ(records.at(0).shadowViewCount, 0U);
    EXPECT_EQ(records.at(1).shadowViewCount, 0U);
    EXPECT_EQ(records.at(2).shadowViewCount, 0U);
}

TEST_F(FrameBindingsTest, PacksOnlyExplicitShadowsAndAllocatesSixPointFaces)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    auto view = makeView(0);
    view.lights = {
        panda::SpotLight {.position = {0.0F, 0.0F, 0.0F},  .range = 8.0F,  .shadow = panda::ShadowSettings {}},
        panda::PointLight {.position = {2.0F, 1.0F, -1.0F}, .range = 12.0F, .shadow = panda::ShadowSettings {}}
    };
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(4)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });

    ASSERT_TRUE(bindings->updateFrame(0, view, {}));

    const auto& plan = bindings->getShadowAtlasPlan(0);
    ASSERT_EQ(plan.views.size(), 7U);
    EXPECT_EQ(plan.firstViewByLight, (std::vector<std::uint32_t> {0U, 1U}));
    EXPECT_EQ(plan.viewCountByLight, (std::vector<std::uint32_t> {1U, 6U}));
    EXPECT_EQ(bindings->getShadowViewOffset(0, 0), 0U);
    EXPECT_EQ(bindings->getShadowViewOffset(0, 1), 256U);
    ASSERT_EQ(writes.size(), 4U);
    const auto shadowRecords = std::ranges::find_if(writes, [](const auto& bytes) {
        return bytes.size() == sizeof(panda::core::vulkan::renderer::ShadowRecord) * 7U;
    });
    ASSERT_NE(shadowRecords, writes.end());
    auto records = std::array<panda::core::vulkan::renderer::ShadowRecord, 7> {};
    std::memcpy(records.data(), shadowRecords->data(), shadowRecords->size());
    EXPECT_FLOAT_EQ(records.front().atlasScaleBias.x, 1024.0F / 4096.0F);
    EXPECT_FLOAT_EQ(records.back().shadowParameters.x, 0.0005F);
    EXPECT_FLOAT_EQ(records.back().shadowParameters.y, 0.002F);
}

TEST_F(FrameBindingsTest, LightBufferGrowthFailureCanBeRetried)
{
    auto created = createBindings();
    ASSERT_TRUE(created);
    auto bindings = std::move(*created);
    const auto largerBufferSize = sizeof(LightRecord) * 2U;
    EXPECT_CALL(buffers,
                createNative(Ref(device),
                             Ref(*pool),
                             testing::Field(&panda::detail::BufferResourceCreateInfo::size, largerBufferSize)))
        .Times(2)
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "buffer growth failed")}}))
        .WillOnce(Return(panda::Result<void> {}));
    auto view = makeView(1);
    view.lights.resize(2);

    const auto failed = bindings->updateFrame(0, view, {});
    const auto retried = bindings->updateFrame(0, view, {});

    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(retried);
}

TEST_F(FrameBindingsTest, RejectsLightCountAboveConfiguredBudget)
{
    auto bindings = createBindings(1);
    ASSERT_TRUE(bindings);
    auto view = makeView(1);
    view.lights.resize(2);

    const auto updated = (*bindings)->updateFrame(0, view, {});

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::ResourceExhausted);
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
            *output = fakeHandle<VkDescriptorPool>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    auto view = makeView(2);
    const auto shadowView = vk::ImageView {fakeHandle<VkImageView>(90)};

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

    const auto empty = FrameBindings::create({.physicalDevice = physical,
                                              .device = device,
                                              .pool = *pool,
                                              .frameCount = 0,
                                              .maxLights = 0,
                                              .textureFilteringPreset = nullptr});
    const auto oversized =
        FrameBindings::create({.physicalDevice = physical,
                               .device = device,
                               .pool = *pool,
                               .frameCount = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1,
                               .maxLights = 0,
                               .textureFilteringPreset = nullptr});
    EXPECT_FALSE(empty);
    EXPECT_FALSE(oversized);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(oversized.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_TRUE(FrameBindings::validateDeviceLimits(physical, 0));
    EXPECT_EQ(FrameBindings::validateDeviceLimits(physical, 129).error().code, panda::ErrorCode::Unsupported);
}

TEST_F(FrameBindingsTest, RejectsMaterialCountBeyondStorageBufferLimit)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            properties->limits.maxStorageBufferRange = static_cast<std::uint32_t>(sizeof(MaterialRecord));
            properties->limits.maxDescriptorSetStorageBuffers = 4;
            properties->limits.maxPerStageDescriptorStorageBuffers = 4;
            properties->limits.maxBoundDescriptorSets = 4;
        });
    auto bindings = createBindings(1);
    ASSERT_TRUE(bindings);

    const auto updated = (*bindings)->updateFrame(0, makeView(2), {});

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, FrameBuffersRejectStorageBufferLimitViolations)
{
    auto limits = vk::PhysicalDeviceLimits {};
    limits.maxStorageBufferRange = 0;
    limits.minUniformBufferOffsetAlignment = 256;

    auto lightBuffers = FrameBuffers {device, *pool, limits, 1};
    auto litView = makeView(0);
    litView.lights.emplace_back(panda::DirectionalLight {});
    const auto tooManyLights = lightBuffers.update(0, litView, {});
    ASSERT_FALSE(tooManyLights);
    EXPECT_EQ(tooManyLights.error().code, panda::ErrorCode::ResourceExhausted);

    limits.maxStorageBufferRange = static_cast<std::uint32_t>(
        std::max({sizeof(MaterialRecord), sizeof(LightRecord), sizeof(panda::core::vulkan::renderer::ShadowRecord)}));
    auto materialBuffers = FrameBuffers {device, *pool, limits, 1};
    ASSERT_TRUE(materialBuffers.initializeFrameBuffers());
    const auto tooManyMaterials = materialBuffers.update(0, makeView(2), {});
    ASSERT_FALSE(tooManyMaterials);
    EXPECT_EQ(tooManyMaterials.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_NE(tooManyMaterials.error().message.find("material storage buffer limits"), std::string::npos);
}

TEST_F(FrameBindingsTest, RejectsDescriptorPoolSizeOverflowBeforeCallingVulkan)
{
    EXPECT_CALL(vulkan, vkCreateDescriptorPool(_, _, _, _)).Times(0);

    const auto tooManySets = panda::core::vulkan::renderer::createDescriptorSets(
        device,
        {},
        {},
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U);
    ASSERT_FALSE(tooManySets);
    EXPECT_EQ(tooManySets.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_NE(tooManySets.error().message.find("Descriptor set count"), std::string::npos);

    const auto multipliedOverflow =
        vk::DescriptorSetLayoutBinding {.binding = 0,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 2,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex};
    const auto tooManyDescriptors =
        panda::core::vulkan::renderer::createDescriptorSets(device,
                                                            {},
                                                            std::span {&multipliedOverflow, std::size_t {1}},
                                                            std::numeric_limits<std::uint32_t>::max());
    ASSERT_FALSE(tooManyDescriptors);
    EXPECT_EQ(tooManyDescriptors.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_NE(tooManyDescriptors.error().message.find("Descriptor pool size"), std::string::npos);

    const auto aggregateOverflow = std::array {
        vk::DescriptorSetLayoutBinding {.binding = 0,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = (std::numeric_limits<std::uint32_t>::max() / 2U) + 1U,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding {.binding = 1,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = (std::numeric_limits<std::uint32_t>::max() / 2U) + 1U,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex}
    };
    const auto tooLargePool = panda::core::vulkan::renderer::createDescriptorSets(device, {}, aggregateOverflow, 1);
    ASSERT_FALSE(tooLargePool);
    EXPECT_EQ(tooLargePool.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_NE(tooLargePool.error().message.find("Descriptor pool size"), std::string::npos);
}

TEST_F(FrameBindingsTest, RejectsBuiltInShaderSetsBeyondDeviceLimit)
{
    auto limits = vk::PhysicalDeviceLimits {};
    limits.maxBoundDescriptorSets = 1;
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    auto descriptors = panda::core::vulkan::renderer::FrameDescriptors {device};
    const auto initialized = descriptors.initialize(limits, 1);

    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(initialized.error().message, "Built-in shader descriptor sets exceed device limits");
}

TEST_F(FrameBindingsTest, RejectsZeroDescriptorSetLimit)
{
    auto limits = vk::PhysicalDeviceLimits {};
    limits.maxBoundDescriptorSets = 0;
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    auto descriptors = panda::core::vulkan::renderer::FrameDescriptors {device};
    const auto initialized = descriptors.initialize(limits, 1);

    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(initialized.error().message, "Built-in shader descriptor sets exceed device limits");
}

TEST_F(FrameBindingsTest, MaterialTextureInitializationPropagatesSamplerFailure)
{
    EXPECT_CALL(vulkan, vkCreateSampler(deviceHandle, NotNull(), _, NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));

    auto textureBindings = MaterialTextureBindings {device, physical, maxSamplerAnisotropy, nullptr};
    const auto initialized = textureBindings.initialize({}, {}, 1);

    ASSERT_FALSE(initialized);
    EXPECT_EQ(initialized.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, PropagatesInvalidShadowViewAndUniformWriteFailures)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    auto invalidView = makeView(0);
    invalidView.lights.emplace_back(panda::DirectionalLight {.shadow = panda::ShadowSettings {}});
    invalidView.camera.viewProjection = glm::mat4 {0.0F};
    const auto invalid = (*bindings)->updateFrame(0, invalidView, {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);

    EXPECT_CALL(buffers, writeMapped(_, 0, _)).WillOnce(Return(bufferFailure()));
    const auto failedWrite = (*bindings)->updateFrame(0, makeView(0), {});
    ASSERT_FALSE(failedWrite);
    EXPECT_EQ(failedWrite.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(FrameBindingsTest, PropagatesShadowRecordWriteFailure)
{
    auto bindings = createBindings();
    ASSERT_TRUE(bindings);
    auto view = makeView(0);
    view.lights.emplace_back(panda::DirectionalLight {.shadow = panda::ShadowSettings {}});
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(3)
        .WillOnce(Return(panda::Result<void> {}))
        .WillOnce(Return(panda::Result<void> {}))
        .WillOnce(Return(bufferFailure()));

    const auto updated = (*bindings)->updateFrame(0, view, {});

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::ResourceExhausted);
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

TEST_F(FrameBindingsTest, UpdatesUnlitViewWithZeroLightBudget)
{
    auto bindings = createBindings(0);
    ASSERT_TRUE(bindings);
    auto view = makeView(1);
    view.meshes.front().material.model = panda::MaterialModel::Unlit;
    auto writes = std::vector<std::vector<std::byte>> {};
    EXPECT_CALL(buffers, writeMapped(_, 0, _))
        .Times(3)
        .WillRepeatedly([&writes](const BufferResource&, std::uint64_t, std::span<const std::byte> data) {
            writes.emplace_back(data.begin(), data.end());
            return panda::Result<void> {};
        });

    const auto updated = (*bindings)->updateFrame(0, view, {});

    ASSERT_TRUE(updated);
    ASSERT_EQ(writes.size(), 3U);
    auto uniform = panda::core::vulkan::renderer::FrameUniform {};
    std::memcpy(&uniform, writes.at(0).data(), sizeof(uniform));
    EXPECT_EQ(uniform.lightInfo.x, 0U);
    auto material = MaterialRecord {};
    std::memcpy(&material, writes.at(1).data(), sizeof(material));
    EXPECT_EQ(material.isLit, 0U);
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
            *output = fakeHandle<VkDescriptorSetLayout>(40);
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
            *output = fakeHandle<VkSampler>(50);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));
    const auto textureSampler = createBindings();
    EXPECT_FALSE(textureSampler);
}

TEST_F(FrameBindingsTest, PropagatesEnvironmentSamplerCreationFailure)
{
    EXPECT_CALL(vulkan, vkCreateSampler(deviceHandle, NotNull(), _, NotNull()))
        .Times(3)
        .WillOnce([this](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* output) {
            *output = fakeHandle<VkSampler>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce([this](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* output) {
            *output = fakeHandle<VkSampler>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory)));

    const auto bindings = createBindings();

    EXPECT_FALSE(bindings);
}

TEST_F(FrameBindingsTest, PropagatesUniformAndMaterialBufferCreationFailures)
{
    EXPECT_CALL(buffers, createNative(Ref(device), Ref(*pool), _)).WillOnce(Return(bufferFailure()));
    const auto uniformBuffer = createBindings();
    ASSERT_FALSE(uniformBuffer);

    EXPECT_CALL(buffers, createNative(Ref(device), Ref(*pool), _))
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
                descriptorSet = fakeHandle<VkDescriptorSet>(nextHandle++);
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
            properties->limits.maxDescriptorSetStorageBuffers = 4;
            properties->limits.maxPerStageDescriptorStorageBuffers = 4;
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
            properties->limits.maxDescriptorSetStorageBuffers = 4;
            properties->limits.maxPerStageDescriptorStorageBuffers = 4;
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
    EXPECT_CALL(textures, createNative(Ref(device), Ref(*pool), _, _))
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

    EXPECT_CALL(textures, createNative(Ref(device), Ref(*pool), _, _))
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
    view.lights.emplace_back(panda::DirectionalLight {.shadow = panda::ShadowSettings {}});
    view.camera.viewProjection = glm::mat4 {0.0F};

    const auto updated = (*renderer)->updateFrame(0, view);

    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(FrameBindingsTest, OneSampleColorPassUsesTheHdrResolveTarget)
{
    auto renderer = createMeshRenderer(VK_SAMPLE_COUNT_1_BIT);
    ASSERT_TRUE(renderer);
    EXPECT_EQ((*renderer)->getSampleCount(), vk::SampleCountFlagBits::e1);

    auto colorPassCount = 0;
    EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull()))
        .Times(2)
        .WillRepeatedly([&](VkCommandBuffer, const VkRenderingInfo* rendering) {
            if (++colorPassCount == 2)
            {
                ASSERT_EQ(rendering->colorAttachmentCount, 1U);
                const auto& attachment = *rendering->pColorAttachments;
                EXPECT_EQ(attachment.imageView, fakeHandle<VkImageView>(90));
                EXPECT_EQ(attachment.loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
                EXPECT_EQ(attachment.storeOp, VK_ATTACHMENT_STORE_OP_STORE);
                EXPECT_EQ(attachment.resolveImageView, VK_NULL_HANDLE);
                EXPECT_EQ(attachment.resolveMode, VK_RESOLVE_MODE_NONE);
            }
        });
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull())).Times(AnyNumber());
    EXPECT_CALL(vulkan, vkCmdSetViewport(_, _, _, NotNull())).Times(1);
    EXPECT_CALL(vulkan, vkCmdSetScissor(_, _, _, NotNull())).Times(1);
    EXPECT_CALL(vulkan, vkCmdBindDescriptorSets(_)).Times(1);
    EXPECT_CALL(vulkan, vkCmdEndRendering(_)).Times(2);
    const auto command = vk::CommandBuffer {fakeHandle<VkCommandBuffer>(901)};
    const auto view = panda::RenderView {};

    (*renderer)->record(command, 0, view);
}

TEST_F(FrameBindingsTest, RecordsEveryPointFaceWithItsOwnDynamicShadowViewOffset)
{
    auto renderer = createMeshRenderer();
    ASSERT_TRUE(renderer);
    auto view = panda::RenderView {};
    view.lights = {
        panda::SpotLight {.position = {0.0F, 0.0F, 0.0F},  .range = 8.0F,  .shadow = panda::ShadowSettings {}},
        panda::PointLight {.position = {2.0F, 1.0F, -1.0F}, .range = 12.0F, .shadow = panda::ShadowSettings {}}
    };
    ASSERT_TRUE((*renderer)->updateFrame(0, view));

    auto offsets = std::vector<std::uint32_t> {};
    auto scissors = std::vector<VkRect2D> {};
    EXPECT_CALL(vulkan, vkCmdBindDescriptorSets(_))
        .Times(8)
        .WillRepeatedly([&offsets](const panda::test::DescriptorSetBindCall& call) {
            ASSERT_EQ(call.dynamicOffsetCount, 1U);
            offsets.push_back(std::span {call.dynamicOffsets, call.dynamicOffsetCount}.front());
        });
    EXPECT_CALL(vulkan, vkCmdSetScissor(_, _, _, NotNull()))
        .Times(8)
        .WillRepeatedly([&scissors](VkCommandBuffer, std::uint32_t, std::uint32_t, const VkRect2D* rect) {
            scissors.push_back(*rect);
        });
    EXPECT_CALL(vulkan, vkCmdSetViewport(_, _, _, NotNull())).Times(8);
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull())).Times(AnyNumber());
    EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull())).Times(2);
    EXPECT_CALL(vulkan, vkCmdEndRendering(_)).Times(2);

    (*renderer)->record(vk::CommandBuffer {fakeHandle<VkCommandBuffer>(902)}, 0, view);

    ASSERT_EQ(offsets.size(), 8U);
    for (auto index = std::uint32_t {}; index < 7U; ++index)
    {
        EXPECT_EQ(offsets.at(index), index * 256U);
    }
    EXPECT_EQ(offsets.back(), 0U);
    ASSERT_EQ(scissors.size(), 8U);
    EXPECT_EQ(scissors.front().offset.x, 4);
    EXPECT_EQ(scissors.front().offset.y, 4);
    EXPECT_EQ(scissors.front().extent.width, 1024U);
    EXPECT_EQ(scissors.at(1).extent.width, 512U);
}

TEST_F(FrameBindingsTest, PropagatesFrameBindingsInitializationFailure)
{
    const auto supportedLimits = [](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
        properties->limits.maxPushConstantsSize = 4096;
        properties->limits.maxStorageBufferRange = 4096;
        properties->limits.maxDescriptorSetStorageBuffers = 4;
        properties->limits.maxPerStageDescriptorStorageBuffers = 4;
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
    EXPECT_CALL(textures, createNative(Ref(device), Ref(*pool), _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(panda::Result<void> {}));

    const auto renderer = panda::core::vulkan::renderer::MeshRenderer::create(meshRendererInfo());

    ASSERT_FALSE(renderer);
    EXPECT_EQ(renderer.error().code, panda::ErrorCode::Unsupported);
}

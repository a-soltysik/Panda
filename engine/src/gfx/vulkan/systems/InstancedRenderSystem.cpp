// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include "panda/gfx/vulkan/systems/InstancedRenderSystem.h"

#include <panda/gfx/vulkan/Context.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "panda/gfx/vulkan/Buffer.h"
#include "panda/gfx/vulkan/Descriptor.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/FrameInfo.h"
#include "panda/gfx/vulkan/Pipeline.h"
#include "panda/gfx/vulkan/Scene.h"
#include "panda/gfx/vulkan/Vertex.h"
#include "panda/gfx/vulkan/object/Mesh.h"
#include "panda/gfx/vulkan/object/Object.h"
#include "panda/gfx/vulkan/object/Surface.h"
#include "panda/gfx/vulkan/object/Texture.h"
#include "panda/internal/config.h"
#include "panda/utils/Utils.h"
#include "panda/utils/format/gfx/api/vulkan/ResultFormatter.h"  // NOLINT(misc-include-cleaner, unused-includes)

namespace panda::gfx::vulkan
{
InstancedRenderSystem::InstancedRenderSystem(const Device& device, vk::RenderPass renderPass, size_t maxInstanceCount)
    : _device {device},
      _descriptorLayout {
          DescriptorSetLayout::Builder(_device)
              .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
              .addBinding(1, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment)
              .addBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
              .addBinding(3, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
              .build(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)},
      _pipelineLayout {createPipelineLayout(_device, _descriptorLayout->getDescriptorSetLayout())},
      _pipeline {createPipeline(_device, renderPass, _pipelineLayout)}
{
    for (auto i = uint32_t {}; i < Context::maxFramesInFlight; i++)
    {
        _instanceBuffers.push_back(std::make_unique<Buffer>(
            device,
            sizeof(InstanceData),
            maxInstanceCount,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            _device.physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment));
        _instanceBuffers.back()->mapWhole();
    }
}

InstancedRenderSystem::~InstancedRenderSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto InstancedRenderSystem::createPipeline(const Device& device,
                                           vk::RenderPass renderPass,
                                           vk::PipelineLayout pipelineLayout) -> std::unique_ptr<Pipeline>
{
    static constexpr auto inputAssemblyInfo =
        vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, vk::False};

    static constexpr auto viewportInfo = vk::PipelineViewportStateCreateInfo {{}, 1, {}, 1, {}};
    static constexpr auto rasterizationInfo =
        vk::PipelineRasterizationStateCreateInfo {{},
                                                  vk::False,
                                                  vk::False,
                                                  vk::PolygonMode::eFill,
                                                  vk::CullModeFlagBits::eBack,
                                                  vk::FrontFace::eCounterClockwise,
                                                  vk::False,
                                                  {},
                                                  {},
                                                  {},
                                                  1.F};

    static constexpr auto multisamplingInfo =
        vk::PipelineMultisampleStateCreateInfo {{}, vk::SampleCountFlagBits::e1, vk::False};
    static constexpr auto colorBlendAttachment =
        vk::PipelineColorBlendAttachmentState {vk::False,
                                               vk::BlendFactor::eSrcAlpha,
                                               vk::BlendFactor::eOneMinusSrcAlpha,
                                               vk::BlendOp::eAdd,
                                               vk::BlendFactor::eOne,
                                               vk::BlendFactor::eZero,
                                               vk::BlendOp::eAdd,
                                               vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    const auto colorBlendInfo =
        vk::PipelineColorBlendStateCreateInfo {{}, vk::False, vk::LogicOp::eCopy, colorBlendAttachment};

    static constexpr auto depthStencilInfo =
        vk::PipelineDepthStencilStateCreateInfo {{}, vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False};

    return std::make_unique<Pipeline>(
        device,
        PipelineConfig {.vertexShaderPath = config::shaderPath / "instanced.vert.spv",
                        .fragmentShaderPath = config::shaderPath / "basic.frag.spv",
                        .vertexBindingDescriptions = {Vertex::getBindingDescription()},
                        .vertexAttributeDescriptions = utils::fromArray(Vertex::getAttributeDescriptions()),
                        .inputAssemblyInfo = inputAssemblyInfo,
                        .viewportInfo = viewportInfo,
                        .rasterizationInfo = rasterizationInfo,
                        .multisamplingInfo = multisamplingInfo,
                        .colorBlendInfo = colorBlendInfo,
                        .depthStencilInfo = depthStencilInfo,
                        .pipelineLayout = pipelineLayout,
                        .renderPass = renderPass,
                        .subpass = 0});
}

auto InstancedRenderSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout)
    -> vk::PipelineLayout
{
    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {{}, setLayout};
    return expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                  vk::Result::eSuccess,
                  "Can't create pipeline layout");
}

auto InstancedRenderSystem::render(const FrameInfo& frameInfo) -> void
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());

    _instances.resize(std::accumulate(frameInfo.scene.getInstancedSurfaceMap().begin(),
                                      frameInfo.scene.getInstancedSurfaceMap().end(),
                                      0,
                                      [](auto value, const auto& mapping) {
                                          return value + mapping.second.size();
                                      }));

    auto index = size_t {};
    for (const auto& surfaces : frameInfo.scene.getInstancedSurfaceMap())
    {
        for (const auto& object : surfaces.second)
        {
            _instances[index++] = {.translation = object->transform.translation,
                                   .scale = object->transform.scale,
                                   .rotation = object->transform.rotation};
        }
    }

    _instanceBuffers[frameInfo.frameIndex]->writeAt(_instances, 0);

    auto baseIndex = size_t {};

    for (const auto& group : frameInfo.scene.getInstancedSurfaceMap())
    {
        DescriptorWriter(*_descriptorLayout)
            .writeBuffer(0, frameInfo.vertUbo.getDescriptorInfo())
            .writeBuffer(1, frameInfo.fragUbo.getDescriptorInfo())
            .writeImage(2, group.first.getTexture().getDescriptorImageInfo())
            .writeBuffer(3, _instanceBuffers[frameInfo.frameIndex]->getDescriptorInfo())
            .push(frameInfo.commandBuffer, _pipelineLayout);

        group.first.getMesh().bind(frameInfo.commandBuffer);
        group.first.getMesh().drawInstanced(frameInfo.commandBuffer, group.second.size(), baseIndex);
        baseIndex += group.second.size();
    }
}
}

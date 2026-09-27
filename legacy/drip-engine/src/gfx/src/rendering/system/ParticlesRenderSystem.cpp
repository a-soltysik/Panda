// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "ParticlesRenderSystem.hpp"

#include <cstdint>
#include <memory>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "drip/gfx/resource/ParticlesRenderable.hpp"
#include "drip/gfx/resource/Renderable.hpp"
#include "drip/gfx/scene/Scene.hpp"
#include "drip/gfx/shaders/Particles.frag.spv.hpp"
#include "drip/gfx/shaders/Particles.vert.spv.hpp"
#include "drip/gfx/utils/format/ResultFormatter.hpp"  // NOLINT(misc-include-cleaner)
#include "rendering/FrameInfo.hpp"
#include "rendering/Renderer.hpp"
#include "vulkan/core/Device.hpp"
#include "vulkan/memory/Buffer.hpp"
#include "vulkan/memory/Descriptor.hpp"
#include "vulkan/memory/SharedBuffer.hpp"
#include "vulkan/pipeline/Pipeline.hpp"

namespace drip::gfx
{

ParticlesRenderSystem::ParticlesRenderSystem(const Device& device, const Renderer& renderer)
    : _device {device},
      _descriptorLayout {DescriptorSetLayout::Builder(_device)
                             .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
                             .addBinding(1, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment)
                             .addBinding(2, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
                             .addBinding(3, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
                             .addBinding(4, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex)
                             .build(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)},
      _pipelineLayout {createPipelineLayout(_device, _descriptorLayout->getDescriptorSetLayout())},
      _pipeline {createPipeline(_device, renderer.getSwapChainRenderPass(), _pipelineLayout)}

{
}

ParticlesRenderSystem::~ParticlesRenderSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto ParticlesRenderSystem::createPipeline(const Device& device,
                                           vk::RenderPass renderPass,
                                           vk::PipelineLayout pipelineLayout) -> std::unique_ptr<Pipeline>
{
    static constexpr auto inputAssemblyInfo =
        vk::PipelineInputAssemblyStateCreateInfo {.topology = vk::PrimitiveTopology::eTriangleList,
                                                  .primitiveRestartEnable = vk::False};

    static constexpr auto viewportInfo = vk::PipelineViewportStateCreateInfo {
        .viewportCount = 1,
        .scissorCount = 1,
    };
    static constexpr auto rasterizationInfo =
        vk::PipelineRasterizationStateCreateInfo {.depthClampEnable = vk::False,
                                                  .rasterizerDiscardEnable = vk::False,
                                                  .polygonMode = vk::PolygonMode::eFill,
                                                  .cullMode = vk::CullModeFlagBits::eBack,
                                                  .frontFace = vk::FrontFace::eCounterClockwise,
                                                  .depthBiasEnable = vk::False,
                                                  .lineWidth = 1.F};

    static constexpr auto multisamplingInfo =
        vk::PipelineMultisampleStateCreateInfo {.rasterizationSamples = vk::SampleCountFlagBits::e1,
                                                .sampleShadingEnable = vk::False};
    static constexpr auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState {
        .blendEnable = vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    static constexpr auto colorBlendInfo =
        vk::PipelineColorBlendStateCreateInfo {.logicOpEnable = vk::False,
                                               .logicOp = vk::LogicOp::eCopy,
                                               .attachmentCount = 1,
                                               .pAttachments = &colorBlendAttachment};

    static constexpr auto depthStencilInfo =
        vk::PipelineDepthStencilStateCreateInfo {.depthTestEnable = vk::True,
                                                 .depthWriteEnable = vk::True,
                                                 .depthCompareOp = vk::CompareOp::eLess,
                                                 .depthBoundsTestEnable = vk::False,
                                                 .stencilTestEnable = vk::False};

    return std::make_unique<Pipeline>(device,
                                      PipelineConfig {.vertexShader = shader::particlesVert,
                                                      .fragmentShader = shader::particlesFrag,
                                                      .vertexBindingDescriptions = {},
                                                      .vertexAttributeDescriptions = {},
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

auto ParticlesRenderSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout)
    -> vk::PipelineLayout
{
    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {.setLayoutCount = 1, .pSetLayouts = &setLayout};
    return common::Expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                          vk::Result::eSuccess,
                          "Can't create pipeline layout")
        .result();
}

void ParticlesRenderSystem::render(const FrameInfo& frameInfo)
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());

    for (const auto& object : frameInfo.scene.getRenderables() | std::ranges::views::filter([](const auto& renderable) {
                                  return renderable->getType() == Renderable::Type::Particles;
                              }))
    {
        renderObject(dynamic_cast<const ParticlesRenderable&>(*object), frameInfo);
    }
}

void ParticlesRenderSystem::renderObject(const ParticlesRenderable& object, const FrameInfo& frameInfo) const
{
    DescriptorWriter(*_descriptorLayout)
        .writeBuffer(0, frameInfo.vertUbo.getDescriptorInfo())
        .writeBuffer(1, frameInfo.fragUbo.getDescriptorInfo())
        .writeBuffer(2, object.getVulkanDataBuffer().translations->getDescriptorInfo())
        .writeBuffer(3, object.getVulkanDataBuffer().colors->getDescriptorInfo())
        .writeBuffer(4, object.getVulkanDataBuffer().sizes->getDescriptorInfo())
        .push(frameInfo.commandBuffer, _pipelineLayout);

    frameInfo.commandBuffer.draw(6, static_cast<uint32_t>(object.getSize()), 0, 0);
}

}

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "MeshRenderSystem.hpp"

#include <cstddef>
#include <drip/common/utils/Utils.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "drip/gfx/resource/Mesh.hpp"
#include "drip/gfx/resource/MeshRenderable.hpp"
#include "drip/gfx/resource/Renderable.hpp"
#include "drip/gfx/resource/Surface.hpp"
#include "drip/gfx/resource/Texture.hpp"
#include "drip/gfx/resource/Vertex.hpp"
#include "drip/gfx/scene/Scene.hpp"
#include "drip/gfx/shaders/Mesh.frag.spv.hpp"
#include "drip/gfx/shaders/Mesh.vert.spv.hpp"
#include "drip/gfx/utils/format/ResultFormatter.hpp"  // NOLINT(misc-include-cleaner)
#include "rendering/FrameInfo.hpp"
#include "rendering/Renderer.hpp"
#include "vulkan/core/Device.hpp"
#include "vulkan/memory/Alignment.hpp"
#include "vulkan/memory/Buffer.hpp"
#include "vulkan/memory/Descriptor.hpp"
#include "vulkan/pipeline/Pipeline.hpp"

namespace drip::gfx
{

namespace
{

struct PushConstantData
{
    DRIP_ALIGNED_MEMBERS((glm::vec3, translation),  //
                         (glm::vec3, scale),        //
                         (glm::vec3, rotation))
};

static_assert(PushConstantData::alignment() == 16, "PushConstantData alignment must be 16 bytes");
static_assert(offsetof(PushConstantData, translation) % 16 == 0, "translation must be 16-byte aligned");
static_assert(offsetof(PushConstantData, scale) % 16 == 0, "scale must be 16-byte aligned");
static_assert(offsetof(PushConstantData, rotation) % 16 == 0, "rotation must be 16-byte aligned");
static_assert(sizeof(PushConstantData) % 16 == 0, "PushConstantData size must be multiple of 16");

}

MeshRenderSystem::MeshRenderSystem(const Device& device, const Renderer& renderer)
    : _device {device},
      _descriptorLayout {
          DescriptorSetLayout::Builder(_device)
              .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
              .addBinding(1, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment)
              .addBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
              .build(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)},
      _pipelineLayout {createPipelineLayout(_device, _descriptorLayout->getDescriptorSetLayout())},
      _pipeline {createPipeline(_device, renderer.getSwapChainRenderPass(), _pipelineLayout)}

{
}

MeshRenderSystem::~MeshRenderSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto MeshRenderSystem::createPipeline(const Device& device,
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

    return std::make_unique<Pipeline>(
        device,
        PipelineConfig {.vertexShader = shader::meshVert,
                        .fragmentShader = shader::meshFrag,
                        .vertexBindingDescriptions = {Vertex::getBindingDescription()},
                        .vertexAttributeDescriptions = common::utils::fromArray(Vertex::getAttributeDescriptions()),
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

auto MeshRenderSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout)
    -> vk::PipelineLayout
{
    static constexpr auto pushConstantData = vk::PushConstantRange {.stageFlags = vk::ShaderStageFlagBits::eVertex,
                                                                    .offset = 0,
                                                                    .size = sizeof(PushConstantData)};

    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {.setLayoutCount = 1,
                                                                  .pSetLayouts = &setLayout,
                                                                  .pushConstantRangeCount = 1,
                                                                  .pPushConstantRanges = &pushConstantData};
    return common::Expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                          vk::Result::eSuccess,
                          "Can't create pipeline layout")
        .result();
}

void MeshRenderSystem::render(const FrameInfo& frameInfo)
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());

    for (const auto& object : frameInfo.scene.getRenderables() | std::ranges::views::filter([](const auto& renderable) {
                                  return renderable->getType() == Renderable::Type::Mesh;
                              }))
    {
        renderObject(dynamic_cast<const MeshRenderable&>(*object), frameInfo);
    }
}

void MeshRenderSystem::renderObject(const MeshRenderable& object, const FrameInfo& frameInfo) const
{
    const auto push = PushConstantData {.translation = object.transform.translation,
                                        .scale = object.transform.scale,
                                        .rotation = object.transform.rotation};

    frameInfo.commandBuffer.pushConstants<PushConstantData>(_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, push);

    for (const auto& surface : object.getSurfaces())
    {
        DescriptorWriter(*_descriptorLayout)
            .writeBuffer(0, frameInfo.vertUbo.getDescriptorInfo())
            .writeBuffer(1, frameInfo.fragUbo.getDescriptorInfo())
            .writeImage(2, surface.getTexture().getDescriptorImageInfo())
            .push(frameInfo.commandBuffer, _pipelineLayout);

        surface.getMesh().bind(frameInfo.commandBuffer);
        surface.getMesh().draw(frameInfo.commandBuffer);
    }
}

}

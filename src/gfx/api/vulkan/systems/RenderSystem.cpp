#include "RenderSystem.h"

namespace panda::gfx::vulkan
{

namespace
{

struct PushConstantData
{
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
};

}

RenderSystem::RenderSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout)
    : _device {device},
      _pipelineLayout {createPipelineLayout(_device, setLayout)},
      _pipeline {createPipeline(_device, renderPass, _pipelineLayout)}
{
}

RenderSystem::~RenderSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto RenderSystem::createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
    -> std::unique_ptr<Pipeline>
{
    const auto inputAssemblyInfo =
        vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};

    const auto viewportInfo = vk::PipelineViewportStateCreateInfo {{}, 1, {}, 1, {}};
    const auto rasterizationInfo = vk::PipelineRasterizationStateCreateInfo {{},
                                                                             VK_FALSE,
                                                                             VK_FALSE,
                                                                             vk::PolygonMode::eFill,
                                                                             vk::CullModeFlagBits::eBack,
                                                                             vk::FrontFace::eCounterClockwise,
                                                                             VK_FALSE,
                                                                             {},
                                                                             {},
                                                                             {},
                                                                             1.f};

    const auto multisamplingInfo = vk::PipelineMultisampleStateCreateInfo {{}, vk::SampleCountFlagBits::e1, VK_FALSE};
    const auto colorBlendAttachment =
        vk::PipelineColorBlendAttachmentState {VK_FALSE,
                                               vk::BlendFactor::eSrcAlpha,
                                               vk::BlendFactor::eOneMinusSrcAlpha,
                                               vk::BlendOp::eAdd,
                                               vk::BlendFactor::eOne,
                                               vk::BlendFactor::eZero,
                                               vk::BlendOp::eAdd,
                                               vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    const auto colorBlendInfo =
        vk::PipelineColorBlendStateCreateInfo {{}, VK_FALSE, vk::LogicOp::eCopy, colorBlendAttachment};

    const auto depthStencilInfo =
        vk::PipelineDepthStencilStateCreateInfo {{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE, VK_FALSE};

    return std::make_unique<Pipeline>(device,
                                      PipelineConfig {"shader/basic.vert.spv",
                                                      "shader/basic.frag.spv",
                                                      {Vertex::getBindingDescription()},
                                                      utils::fromArray(Vertex::getAttributeDescriptions()),
                                                      inputAssemblyInfo,
                                                      viewportInfo,
                                                      rasterizationInfo,
                                                      multisamplingInfo,
                                                      colorBlendInfo,
                                                      depthStencilInfo,
                                                      pipelineLayout,
                                                      renderPass,
                                                      0});
}

auto RenderSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout
{
    const auto pushConstantData =
        vk::PushConstantRange {vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                               0,
                               sizeof(PushConstantData)};

    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {{}, setLayout, pushConstantData};
    return expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                  vk::Result::eSuccess,
                  "Can't create pipeline layout");
}

auto RenderSystem::render(const FrameInfo& frameInfo, std::vector<Object>& objects) const -> void
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());
    frameInfo.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               _pipelineLayout,
                                               0,
                                               frameInfo.descriptorSet,
                                               {});

    for (auto& object : objects)
    {
        const auto push = PushConstantData {object.transform.mat4(), object.transform.normalMatrix()};

        frameInfo.commandBuffer.pushConstants(_pipelineLayout,
                                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                              0,
                                              sizeof(PushConstantData),
                                              &push);
        object.mesh->bind(frameInfo.commandBuffer);
        object.mesh->draw(frameInfo.commandBuffer);
    }
}

}

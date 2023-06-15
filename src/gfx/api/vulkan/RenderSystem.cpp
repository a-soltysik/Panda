#include "RenderSystem.h"

#include <glm/gtc/constants.hpp>

namespace panda::gfx::vulkan
{

namespace
{

struct PushConstantData
{
    glm::mat2 transform;
    glm::vec2 offset;
    alignas(16) glm::vec3 color;
};

}

RenderSystem::RenderSystem(const Device& deviceRef, vk::RenderPass renderPass)
    : device {deviceRef},
      pipelineLayout {createPipelineLayout(device)},
      pipeline {createPipeline(device, renderPass, pipelineLayout)}
{
}

RenderSystem::~RenderSystem()
{
    device.logicalDevice.destroyPipelineLayout(pipelineLayout);
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
                                                                             vk::FrontFace::eClockwise,
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

    return std::make_unique<Pipeline>(device,
                                      PipelineConfig {"shader/triangle.vert.spv",
                                                      "shader/triangle.frag.spv",
                                                      inputAssemblyInfo,
                                                      viewportInfo,
                                                      rasterizationInfo,
                                                      multisamplingInfo,
                                                      colorBlendInfo,
                                                      {},
                                                      pipelineLayout,
                                                      renderPass,
                                                      0});
}

auto RenderSystem::createPipelineLayout(const Device& device) -> vk::PipelineLayout
{
    const auto pushConstantData =
        vk::PushConstantRange {vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                               0,
                               sizeof(PushConstantData)};

    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {{}, {}, pushConstantData};
    return expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                  vk::Result::eSuccess,
                  "Can't create pipeline layout");
}

auto RenderSystem::render(vk::CommandBuffer commandBuffer, std::vector<Object>& objects) const -> void
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->getHandle());

    for (auto& object : objects)
    {
        object.transform.rotation = glm::mod(object.transform.rotation + 0.01f, glm::two_pi<float>());
        const auto push = PushConstantData {object.transform.mat2(), object.transform.translation, object.color};

        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0,
                                    sizeof(PushConstantData),
                                    &push);
        object.mesh->bind(commandBuffer);
        object.mesh->draw(commandBuffer);
    }
}

}

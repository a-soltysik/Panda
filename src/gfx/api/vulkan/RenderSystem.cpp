#include "RenderSystem.h"

#include <glm/gtc/constants.hpp>

namespace panda::gfx::vulkan
{

namespace
{

struct PushConstantData
{
    glm::mat4 transform {1.f};
    alignas(16) glm::mat4 normalMatrix {};
};

}

RenderSystem::RenderSystem(const Device& device, vk::RenderPass renderPass)
    : _device {device},
      _pipelineLayout {createPipelineLayout(_device)},
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

auto RenderSystem::render(vk::CommandBuffer commandBuffer, std::vector<Object>& objects, const Camera& camera) const
    -> void
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());

    auto projectionView = camera.getProjection() * camera.getView();

    for (auto& object : objects)
    {
        //object.transform.rotation.y = glm::mod(object.transform.rotation.y + 0.001f, glm::two_pi<float>());
        //object.transform.rotation.x = glm::mod(object.transform.rotation.x + 0.0005f, glm::two_pi<float>());
        //object.transform.rotation.z = glm::mod(object.transform.rotation.z + 0.0002f, glm::two_pi<float>());
        const auto push = PushConstantData {projectionView * object.transform.mat4(), object.transform.normalMatrix()};

        commandBuffer.pushConstants(_pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0,
                                    sizeof(PushConstantData),
                                    &push);
        object.mesh->bind(commandBuffer);
        object.mesh->draw(commandBuffer);
    }
}

}

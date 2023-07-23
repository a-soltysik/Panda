#include "PointLightSystem.h"

namespace
{

struct LightPushConstants
{
    glm::vec4 position;
    glm::vec4 color;
    float radius;
    bool isPointLight;
};

}

namespace panda::gfx::vulkan
{

PointLightSystem::PointLightSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout)
    : _device {device},
      _pipelineLayout {createPipelineLayout(_device, setLayout)},
      _pipeline {createPipeline(_device, renderPass, _pipelineLayout)}
{
}

PointLightSystem::~PointLightSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto PointLightSystem::createPipeline(const Device& device,
                                      vk::RenderPass renderPass,
                                      vk::PipelineLayout pipelineLayout) -> std::unique_ptr<Pipeline>
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
                                      PipelineConfig {"shader/pointLight.vert.spv",
                                                      "shader/pointLight.frag.spv",
                                                      {},
                                                      {},
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

auto PointLightSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout)
    -> vk::PipelineLayout
{
    const auto pushConstantData =
        vk::PushConstantRange {vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                               0,
                               sizeof(LightPushConstants)};

    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo {{}, setLayout, pushConstantData};
    return expect(device.logicalDevice.createPipelineLayout(pipelineLayoutInfo),
                  vk::Result::eSuccess,
                  "Can't create pipeline layout");
}

auto PointLightSystem::render(const FrameInfo& frameInfo, std::span<const Light> lights) const -> void
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());
    frameInfo.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               _pipelineLayout,
                                               0,
                                               frameInfo.descriptorSet,
                                               {});

    for (const auto& light : lights)
    {
        std::visit(utils::overload {[this, &frameInfo](const DirectionalLight&) {
                                        const auto pushConstant = LightPushConstants {{}, {}, {}, false};
                                        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
                                            _pipelineLayout,
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                            0,
                                            pushConstant);
                                    },
                                    [this, &frameInfo](const PointLight& pointLight) {
                                        const auto pushConstant = LightPushConstants {
                                            {pointLight.position, 1.f                 },
                                            {pointLight.color,    pointLight.intensity},
                                            pointLight.radius,
                                            true
                                        };
                                        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
                                            _pipelineLayout,
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                            0,
                                            pushConstant);
                                    }},
                   light);
        frameInfo.commandBuffer.draw(6, 1, 0, 0);
    }
}

auto PointLightSystem::update(std::span<const Light> lights, GlobalUbo& ubo) -> void
{
    auto directionalLightIndex = uint32_t {};
    auto pointLightIndex = uint32_t {};

    for (const auto& light : lights)
    {
        std::visit(utils::overload {[&directionalLightIndex, &ubo](const DirectionalLight& directionalLight) {
                                        ubo.directionalLights[directionalLightIndex++] =
                                            fromDirectionalLight(directionalLight);
                                    },
                                    [&pointLightIndex, &ubo](const PointLight& pointLight) {
                                        ubo.pointLights[pointLightIndex++] = fromPointLight(pointLight);
                                    }},
                   light);
    }

    ubo.activeDirectionalLights = directionalLightIndex;
    ubo.activePointLights = pointLightIndex;
}
}

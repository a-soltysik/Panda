#include "PointLightSystem.h"

#include "panda/internal/config.h"

namespace
{

struct LightPushConstants
{
    glm::vec4 position;
    glm::vec4 color;
    float radius;
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
                                                                             1.F};

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
                                      PipelineConfig {config::shaderPath / "pointLight.vert.spv",
                                                      config::shaderPath / "pointLight.frag.spv",
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

auto PointLightSystem::render(const FrameInfo& frameInfo, const Lights& lights) const -> void
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());
    frameInfo.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               _pipelineLayout,
                                               0,
                                               frameInfo.descriptorSet,
                                               {});

    for (const auto& light : lights.pointLights)
    {
        const auto pushConstant = LightPushConstants {
            {light.position, 1.F            },
            {light.diffuse,  light.intensity},
            light.intensity / 10.F,
        };
        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
            _pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstant);
        frameInfo.commandBuffer.draw(6, 1, 0, 0);
    }

    for (const auto& light : lights.spotLights)
    {
        const auto pushConstant = LightPushConstants {
            {light.position, 1.F            },
            {light.diffuse,  light.intensity},
            light.intensity / 10.F,
        };
        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
            _pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstant);
        frameInfo.commandBuffer.draw(6, 1, 0, 0);
    }
}

auto PointLightSystem::update(const Lights& lights, FragUbo& ubo) -> void
{
    for (auto i = size_t {}; i < ubo.directionalLights.size() && i < lights.directionalLights.size(); i++)
    {
        ubo.directionalLights[i] = fromDirectionalLight(lights.directionalLights[i]);
    }
    for (auto i = size_t {}; i < ubo.pointLights.size() && i < lights.pointLights.size(); i++)
    {
        ubo.pointLights[i] = fromPointLight(lights.pointLights[i]);
    }
    for (auto i = size_t {}; i < ubo.spotLights.size() && i < lights.spotLights.size(); i++)
    {
        ubo.spotLights[i] = fromSpotLight(lights.spotLights[i]);
    }

    ubo.activeDirectionalLights = lights.directionalLights.size();
    ubo.activePointLights = lights.pointLights.size();
    ubo.activeSpotLights = lights.spotLights.size();
}
}

// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include "panda/gfx/vulkan/systems/LightSystem.h"

#include <cstddef>
#include <glm/ext/vector_float4.hpp>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "panda/gfx/Light.h"
#include "panda/gfx/vulkan/Descriptor.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/FrameInfo.h"
#include "panda/gfx/vulkan/Pipeline.h"
#include "panda/gfx/vulkan/UboLight.h"
#include "panda/internal/config.h"
#include "panda/utils/format/gfx/api/vulkan/ResultFormatter.h"  // NOLINT(misc-include-cleaner)

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

LightSystem::LightSystem(const Device& device, vk::RenderPass renderPass)
    : _device {device},
      _descriptorLayout {DescriptorSetLayout::Builder(_device)
                             .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
                             .build(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)},
      _pipelineLayout {createPipelineLayout(_device, _descriptorLayout->getDescriptorSetLayout())},
      _pipeline {createPipeline(_device, renderPass, _pipelineLayout)}
{
}

LightSystem::~LightSystem() noexcept
{
    _device.logicalDevice.destroyPipelineLayout(_pipelineLayout);
}

auto LightSystem::createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
    -> std::unique_ptr<Pipeline>
{
    const auto inputAssemblyInfo =
        vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, vk::False};

    const auto viewportInfo = vk::PipelineViewportStateCreateInfo {{}, 1, {}, 1, {}};
    const auto rasterizationInfo = vk::PipelineRasterizationStateCreateInfo {{},
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

    const auto multisamplingInfo = vk::PipelineMultisampleStateCreateInfo {{}, vk::SampleCountFlagBits::e1, vk::False};
    const auto colorBlendAttachment =
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

    const auto depthStencilInfo =
        vk::PipelineDepthStencilStateCreateInfo {{}, vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False};

    return std::make_unique<Pipeline>(device,
                                      PipelineConfig {.vertexShaderPath = config::shaderPath / "pointLight.vert.spv",
                                                      .fragmentShaderPath = config::shaderPath / "pointLight.frag.spv",
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

auto LightSystem::createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout
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

auto LightSystem::render(const FrameInfo& frameInfo) const -> void
{
    frameInfo.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline->getHandle());

    DescriptorWriter(*_descriptorLayout)
        .writeBuffer(0, frameInfo.vertUbo.getDescriptorInfo())
        .push(frameInfo.commandBuffer, _pipelineLayout);
    static constexpr auto cubeVerticesCount = 6;
    for (const auto& light : frameInfo.scene.getLights().pointLights)
    {
        const auto pushConstant = LightPushConstants {
            .position = {light.position, 1.F            },
            .color = {light.diffuse,  light.intensity},
            .radius = light.intensity / 10.F,
        };
        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
            _pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstant);

        frameInfo.commandBuffer.draw(cubeVerticesCount, 1, 0, 0);
    }

    for (const auto& light : frameInfo.scene.getLights().spotLights)
    {
        const auto pushConstant = LightPushConstants {
            .position = {light.position, 1.F            },
            .color = {light.diffuse,  light.intensity},
            .radius = light.intensity / 10.F,
        };
        frameInfo.commandBuffer.pushConstants<LightPushConstants>(
            _pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstant);
        frameInfo.commandBuffer.draw(cubeVerticesCount, 1, 0, 0);
    }
}

auto LightSystem::update(const Lights& lights, FragUbo& ubo) -> void
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

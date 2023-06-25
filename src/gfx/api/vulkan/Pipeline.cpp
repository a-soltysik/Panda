#include "Pipeline.h"

#include "Shader.h"
#include "Vertex.h"

namespace panda::gfx::vulkan
{

Pipeline::Pipeline(const Device& device, const PipelineConfig& config)
    : _pipeline {createPipeline(device, config)},
      _device {device}
{
}

Pipeline::~Pipeline() noexcept
{
    log::Info("Destroying pipeline");
    _device.logicalDevice.destroy(_pipeline);
}

auto Pipeline::createPipeline(const Device& device, const PipelineConfig& config) -> vk::Pipeline
{
    const auto vertexShader = Shader::createFromFile(device.logicalDevice, config.vertexShaderPath);
    const auto fragmentShader = Shader::createFromFile(device.logicalDevice, config.fragmentShaderPath);

    auto shaderStages = std::vector<vk::PipelineShaderStageCreateInfo> {};

    if (vertexShader.has_value())
    {
        shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags {},
                                  vk::ShaderStageFlagBits::eVertex,
                                  vertexShader->module,
                                  Shader::getEntryPointName());
    }
    if (fragmentShader.has_value())
    {
        shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags {},
                                  vk::ShaderStageFlagBits::eFragment,
                                  fragmentShader->module,
                                  Shader::getEntryPointName());
    }

    const auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo {{}, dynamicStates};

    static constexpr auto bindingDescription = Vertex::getBindingDescription();
    static constexpr auto attributeDescriptions = Vertex::getAttributeDescriptions();

    const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo {{}, bindingDescription, attributeDescriptions};

    const auto pipelineInfo = vk::GraphicsPipelineCreateInfo {{},
                                                              shaderStages,
                                                              &vertexInputInfo,
                                                              &config.inputAssemblyInfo,
                                                              {},
                                                              &config.viewportInfo,
                                                              &config.rasterizationInfo,
                                                              &config.multisamplingInfo,
                                                              &config.depthStencilInfo,
                                                              &config.colorBlendInfo,
                                                              &dynamicState,
                                                              config.pipelineLayout,
                                                              config.renderPass,
                                                              config.subpass};

    return expect(device.logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo),
                  vk::Result::eSuccess,
                  "Cannot create pipeline");
}

auto Pipeline::getHandle() const noexcept -> const vk::Pipeline&
{
    return _pipeline;
}

}

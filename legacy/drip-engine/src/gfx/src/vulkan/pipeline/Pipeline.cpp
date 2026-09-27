// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "Pipeline.hpp"

#include <array>
#include <cstdint>
#include <drip/common/log/LogMessageBuilder.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "Shader.hpp"
#include "drip/gfx/utils/format/ResultFormatter.hpp"  //NOLINT(misc-include-cleaner)
#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

Pipeline::Pipeline(const Device& device, const PipelineConfig& config)
    : _pipeline {createPipeline(device, config)},
      _device {device}
{
}

Pipeline::~Pipeline() noexcept
{
    common::log::Info("Destroying pipeline");
    _device.logicalDevice.destroy(_pipeline);
}

auto Pipeline::createPipeline(const Device& device, const PipelineConfig& config) -> vk::Pipeline
{
    const auto vertexShader = Shader::create(device.logicalDevice, config.vertexShader, Shader::Type::Vertex);
    const auto fragmentShader = Shader::create(device.logicalDevice, config.fragmentShader, Shader::Type::Fragment);

    auto shaderStages = std::vector<vk::PipelineShaderStageCreateInfo> {};

    if (vertexShader.has_value())
    {
        shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eVertex,
                                                                     .module = vertexShader->module,
                                                                     .pName = Shader::getEntryPointName()});
    }
    if (fragmentShader.has_value())
    {
        shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo {.stage = vk::ShaderStageFlagBits::eFragment,
                                                                     .module = fragmentShader->module,
                                                                     .pName = Shader::getEntryPointName()});
    }

    static constexpr auto dynamicStates = std::array {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    static constexpr auto dynamicState = vk::PipelineDynamicStateCreateInfo {.dynamicStateCount = dynamicStates.size(),
                                                                             .pDynamicStates = dynamicStates.data()};

    const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo {
        .vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindingDescriptions.size()),
        .pVertexBindingDescriptions = config.vertexBindingDescriptions.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributeDescriptions.size()),
        .pVertexAttributeDescriptions = config.vertexAttributeDescriptions.data()};

    const auto pipelineInfo = vk::GraphicsPipelineCreateInfo {.stageCount = static_cast<uint32_t>(shaderStages.size()),
                                                              .pStages = shaderStages.data(),
                                                              .pVertexInputState = &vertexInputInfo,
                                                              .pInputAssemblyState = &config.inputAssemblyInfo,
                                                              .pViewportState = &config.viewportInfo,
                                                              .pRasterizationState = &config.rasterizationInfo,
                                                              .pMultisampleState = &config.multisamplingInfo,
                                                              .pDepthStencilState = &config.depthStencilInfo,
                                                              .pColorBlendState = &config.colorBlendInfo,
                                                              .pDynamicState = &dynamicState,
                                                              .layout = config.pipelineLayout,
                                                              .renderPass = config.renderPass,
                                                              .subpass = config.subpass};

    return common::Expect(device.logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo),
                          vk::Result::eSuccess,
                          "Cannot create pipeline")
        .result();
}

auto Pipeline::getHandle() const noexcept -> const vk::Pipeline&
{
    return _pipeline;
}

}

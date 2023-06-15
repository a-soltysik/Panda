#pragma once

#include "Device.h"

#include <filesystem>

namespace panda::gfx::vulkan
{

struct PipelineConfig
{
    std::filesystem::path vertexShaderPath;
    std::filesystem::path fragmentShaderPath;

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
    vk::PipelineViewportStateCreateInfo viewportInfo;
    vk::PipelineRasterizationStateCreateInfo rasterizationInfo;
    vk::PipelineMultisampleStateCreateInfo multisamplingInfo;
    vk::PipelineColorBlendStateCreateInfo colorBlendInfo;
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
    vk::PipelineLayout pipelineLayout;
    vk::RenderPass renderPass;
    uint32_t subpass;

};

class Pipeline
{
public:
    Pipeline(const Device& device, const PipelineConfig& config);
    PD_DELETE_ALL(Pipeline);
    ~Pipeline() noexcept;

    [[nodiscard]] auto getHandle() const noexcept -> const vk::Pipeline&;
private:
    [[nodiscard]] static auto createPipeline(const Device& device, const PipelineConfig& config) -> vk::Pipeline;

    vk::Pipeline pipeline;
    const Device& device;
};

}

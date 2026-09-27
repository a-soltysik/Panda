#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

struct PipelineConfig
{
    std::span<const uint32_t> vertexShader;
    std::span<const uint32_t> fragmentShader;

    std::vector<vk::VertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions;
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
    vk::PipelineViewportStateCreateInfo viewportInfo;
    vk::PipelineRasterizationStateCreateInfo rasterizationInfo;
    vk::PipelineMultisampleStateCreateInfo multisamplingInfo;
    vk::PipelineColorBlendStateCreateInfo colorBlendInfo;
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
    vk::PipelineLayout pipelineLayout;
    vk::RenderPass renderPass;
    uint32_t subpass = 0;
};

class Pipeline
{
public:
    Pipeline(const Device& device, const PipelineConfig& config);
    Pipeline(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    auto operator=(const Pipeline&) = delete;
    auto operator=(Pipeline&&) = delete;
    ~Pipeline() noexcept;

    [[nodiscard]] auto getHandle() const noexcept -> const vk::Pipeline&;

private:
    [[nodiscard]] static auto createPipeline(const Device& device, const PipelineConfig& config) -> vk::Pipeline;

    vk::Pipeline _pipeline;
    const Device& _device;
};

}

#pragma once

#include "panda/gfx/Camera.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/FrameInfo.h"
#include "panda/gfx/vulkan/Pipeline.h"
#include "panda/gfx/vulkan/Object.h"

namespace panda::gfx::vulkan
{

class RenderSystem
{
public:
    RenderSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout);
    PD_DELETE_ALL(RenderSystem);
    ~RenderSystem() noexcept;

    auto render(const FrameInfo& frameInfo, std::span<const Object> objects) const -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    const Device& _device;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

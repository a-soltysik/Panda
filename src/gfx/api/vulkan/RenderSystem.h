#pragma once

#include "FrameInfo.h"
#include "Object.h"
#include "Pipeline.h"
#include "gfx/Camera.h"

namespace panda::gfx::vulkan
{

class RenderSystem
{
public:
    RenderSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout);
    PD_DELETE_ALL(RenderSystem);
    ~RenderSystem() noexcept;

    auto render(const FrameInfo& frameInfo, std::vector<Object>& objects) const -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    const Device& _device;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

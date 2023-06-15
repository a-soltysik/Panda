#pragma once

#include "Object.h"
#include "Pipeline.h"

namespace panda::gfx::vulkan
{

class RenderSystem
{
public:
    RenderSystem(const Device& deviceRef, vk::RenderPass renderPass);
    PD_DELETE_ALL(RenderSystem);
    ~RenderSystem() noexcept;

    auto render(vk::CommandBuffer commandBuffer, std::vector<Object>& objects) const -> void;
private:
    static auto createPipelineLayout(const Device& device) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout) -> std::unique_ptr<Pipeline>;

    const Device& device;
    vk::PipelineLayout pipelineLayout;
    std::unique_ptr<Pipeline> pipeline;

};

}

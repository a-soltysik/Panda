#pragma once

#include "panda/gfx/Camera.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/FrameInfo.h"
#include "panda/gfx/vulkan/Object.h"
#include "panda/gfx/vulkan/Pipeline.h"

namespace panda::gfx::vulkan
{

class PointLightSystem
{
public:
    PointLightSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout);
    PD_DELETE_ALL(PointLightSystem);
    ~PointLightSystem() noexcept;

    static auto update(const Lights& lights, FragUbo& ubo) -> void;
    auto render(const FrameInfo& frameInfo, const Lights& lights) const -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    const Device& _device;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

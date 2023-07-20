#pragma once

#include "gfx/Camera.h"
#include "gfx/api/vulkan/Device.h"
#include "gfx/api/vulkan/FrameInfo.h"
#include "gfx/api/vulkan/Object.h"
#include "gfx/api/vulkan/Pipeline.h"

namespace panda::gfx::vulkan
{

class PointLightSystem
{
public:
    PointLightSystem(const Device& device, vk::RenderPass renderPass, vk::DescriptorSetLayout setLayout);
    PD_DELETE_ALL(PointLightSystem);
    ~PointLightSystem() noexcept;

    auto render(const FrameInfo& frameInfo) const -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    const Device& _device;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

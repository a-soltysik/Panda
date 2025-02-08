#pragma once

// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include <memory>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "panda/Common.h"
#include "panda/gfx/Light.h"
#include "panda/gfx/vulkan/Descriptor.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/FrameInfo.h"
#include "panda/gfx/vulkan/Pipeline.h"

namespace panda::gfx::vulkan
{

class LightSystem
{
public:
    LightSystem(const Device& device, vk::RenderPass renderPass);
    PD_DELETE_ALL(LightSystem);
    ~LightSystem() noexcept;

    static auto update(const Lights& lights, FragUbo& ubo) -> void;
    auto render(const FrameInfo& frameInfo) const -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    const Device& _device;
    std::unique_ptr<DescriptorSetLayout> _descriptorLayout;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

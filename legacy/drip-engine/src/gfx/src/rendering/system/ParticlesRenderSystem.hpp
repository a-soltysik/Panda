#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <memory>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "RenderSystem.hpp"
#include "drip/gfx/resource/ParticlesRenderable.hpp"
#include "rendering/FrameInfo.hpp"
#include "rendering/Renderer.hpp"
#include "vulkan/memory/Descriptor.hpp"
#include "vulkan/pipeline/Pipeline.hpp"

namespace drip::gfx
{

class ParticlesRenderSystem : public RenderSystem
{
public:
    ParticlesRenderSystem(const Device& device, const Renderer& renderer);
    ParticlesRenderSystem(const ParticlesRenderSystem&) = delete;
    ParticlesRenderSystem(ParticlesRenderSystem&&) = delete;
    auto operator=(const ParticlesRenderSystem&) = delete;
    auto operator=(ParticlesRenderSystem&&) = delete;
    ~ParticlesRenderSystem() noexcept override;

    void render(const FrameInfo& frameInfo) override;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    void renderObject(const ParticlesRenderable& object, const FrameInfo& frameInfo) const;

    const Device& _device;
    std::unique_ptr<DescriptorSetLayout> _descriptorLayout;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
};

}

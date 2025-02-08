#pragma once

// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "panda/Common.h"
#include "panda/gfx/vulkan/Alignment.h"
#include "panda/gfx/vulkan/Buffer.h"
#include "panda/gfx/vulkan/Pipeline.h"

namespace panda::gfx::vulkan
{
class DescriptorSetLayout;
class Device;
struct FrameInfo;

class InstancedRenderSystem
{
public:
    InstancedRenderSystem(const Device& device, vk::RenderPass renderPass, size_t maxInstanceCount);
    PD_DELETE_ALL(InstancedRenderSystem);
    ~InstancedRenderSystem() noexcept;

    auto render(const FrameInfo& frameInfo) -> void;

private:
    static auto createPipelineLayout(const Device& device, vk::DescriptorSetLayout setLayout) -> vk::PipelineLayout;
    static auto createPipeline(const Device& device, vk::RenderPass renderPass, vk::PipelineLayout pipelineLayout)
        -> std::unique_ptr<Pipeline>;

    struct InstanceData
    {
        alignas(16) glm::vec3 translation;
        alignas(16) glm::vec3 scale;
        alignas(16) glm::vec3 rotation;

        PD_MAKE_ALIGNED(translation, scale, rotation)
    };

    const Device& _device;
    std::unique_ptr<DescriptorSetLayout> _descriptorLayout;
    vk::PipelineLayout _pipelineLayout;
    std::unique_ptr<Pipeline> _pipeline;
    std::vector<std::unique_ptr<Buffer>> _instanceBuffers;
    std::vector<InstanceData> _instances;
};

}

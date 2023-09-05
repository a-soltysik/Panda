#pragma once

#include "panda/gfx/Camera.h"
#include "panda/gfx/vulkan/Alignment.h"
#include "panda/gfx/vulkan/UboLight.h"

namespace panda::gfx::vulkan
{

struct FrameInfo
{
    const Camera& camera;
    vk::CommandBuffer commandBuffer;
    vk::DescriptorSet descriptorSet;

    uint32_t frameIndex;
    float deltaTime;
};

struct GlobalUbo
{
    template <typename T>
    using LightArray = std::array<T, 6>;

    PD_ALIGN(glm::mat4) projection {1.f};
    PD_ALIGN(glm::mat4) view {1.f};
    PD_ALIGN(glm::mat4) inverseView {1.f};

    PD_ALIGN(glm::vec4) ambientColor;
    PD_ALIGN(LightArray<UboPointLight>) pointLights;
    PD_ALIGN(LightArray<UboDirectionalLight>) directionalLights;
    PD_ALIGN(uint32_t) activePointLights;
    PD_ALIGN(uint32_t) activeDirectionalLights;
};

}

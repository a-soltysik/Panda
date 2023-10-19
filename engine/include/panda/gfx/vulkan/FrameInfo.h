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

struct VertUbo
{
    PD_ALIGN(glm::mat4) projection {1.f};
    PD_ALIGN(glm::mat4) view {1.f};
};

struct FragUbo
{
    template <typename T>
    using LightArray = std::array<T, 5>;
    PD_ALIGN(glm::mat4) inverseView {1.f};

    //PD_ALIGN(UboMaterial) material;
    PD_ALIGN(LightArray<UboPointLight>) pointLights;
    PD_ALIGN(LightArray<UboDirectionalLight>) directionalLights;
    PD_ALIGN(LightArray<UboSpotLight>) spotLights;
    PD_ALIGN(glm::vec3) ambientColor;
    PD_ALIGN(uint32_t) activePointLights;
    PD_ALIGN(uint32_t) activeDirectionalLights;
    PD_ALIGN(uint32_t) activeSpotLights;
};

}

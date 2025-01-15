#pragma once

#include "Buffer.h"
#include "Descriptor.h"
#include "panda/gfx/Camera.h"
#include "panda/gfx/vulkan/Alignment.h"
#include "panda/gfx/vulkan/UboLight.h"

namespace panda::gfx::vulkan
{

struct FrameInfo
{
    const Camera& camera;
    const Device& device;
    const Buffer& fragUbo;
    const Buffer& vertUbo;
    const DescriptorSetLayout& descriptorSetLayout;
    vk::CommandBuffer commandBuffer;

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
    glm::mat4 inverseView {1.f};

    //PD_ALIGN(UboMaterial) material;
    LightArray<UboPointLight> pointLights;
    LightArray<UboDirectionalLight> directionalLights;
    LightArray<UboSpotLight> spotLights;
    glm::vec3 ambientColor;
    uint32_t activePointLights;
    uint32_t activeDirectionalLights;
    uint32_t activeSpotLights;
};

}

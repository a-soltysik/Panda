#pragma once

// clang-format off
#include "panda/utils/Assert.h"
// clang-format on

#include <array>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "Buffer.h"
#include "panda/gfx/vulkan/Alignment.h"
#include "panda/gfx/vulkan/Scene.h"
#include "panda/gfx/vulkan/UboLight.h"

namespace panda::gfx::vulkan
{

struct FrameInfo
{
    const Scene& scene;
    const Buffer& fragUbo;
    const Buffer& vertUbo;
    vk::CommandBuffer commandBuffer;

    uint32_t frameIndex;
    float deltaTime;
};

struct VertUbo
{
    glm::mat4 projection {1.F};
    glm::mat4 view {1.F};
};

struct FragUbo
{
    template <typename T>
    using LightArray = std::array<T, 5>;
    glm::mat4 inverseView {1.F};

    LightArray<UboPointLight> pointLights;
    LightArray<UboDirectionalLight> directionalLights;
    LightArray<UboSpotLight> spotLights;
    glm::vec3 ambientColor;
    uint32_t activePointLights;
    uint32_t activeDirectionalLights;
    uint32_t activeSpotLights;
};

}

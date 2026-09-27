#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <array>
#include <cstdint>
#include <drip/gfx/scene/Scene.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "UboLight.hpp"
#include "vulkan/memory/Alignment.hpp"
#include "vulkan/memory/Buffer.hpp"

namespace drip::gfx
{

struct FrameInfo
{
    const Scene& scene;
    const Buffer& fragUbo;
    const Buffer& vertUbo;
    vk::CommandBuffer commandBuffer;

    uint32_t frameIndex;
};

struct VertUbo
{
    DRIP_ALIGNED_MEMBERS((glm::mat4, projection),  //
                         (glm::mat4, view))
};

static_assert(VertUbo::alignment() == 16);
static_assert(sizeof(VertUbo) == 2 * sizeof(glm::mat4));
static_assert(offsetof(VertUbo, projection) == 0);
static_assert(offsetof(VertUbo, view) == sizeof(glm::mat4));

struct FragUbo
{
    template <typename T>
    using LightArray = std::array<T, 5>;

    DRIP_ALIGNED_MEMBERS((glm::mat4, inverseView),
                         (LightArray<UboPointLight>, pointLights),
                         (LightArray<UboDirectionalLight>, directionalLights),
                         (LightArray<UboSpotLight>, spotLights),
                         (glm::vec3, ambientColor),
                         (uint32_t, activePointLights),
                         (uint32_t, activeDirectionalLights),
                         (uint32_t, activeSpotLights))
};

// Compile-time tests for FragUbo
static_assert(FragUbo::alignment() == 16, "FragUbo must be 16-byte aligned for std140");
static_assert(offsetof(FragUbo, inverseView) == 0, "inverseView at offset 0");
static_assert(offsetof(FragUbo, inverseView) % 16 == 0, "inverseView must be 16-byte aligned");
static_assert(offsetof(FragUbo, pointLights) % 16 == 0, "pointLights array must be 16-byte aligned");
static_assert(offsetof(FragUbo, directionalLights) % 16 == 0, "directionalLights array must be 16-byte aligned");
static_assert(offsetof(FragUbo, spotLights) % 16 == 0, "spotLights array must be 16-byte aligned");
static_assert(offsetof(FragUbo, ambientColor) % 16 == 0, "ambientColor must be 16-byte aligned");
static_assert(offsetof(FragUbo, activePointLights) % 4 == 0, "activePointLights must be 4-byte aligned");
static_assert(offsetof(FragUbo, activeDirectionalLights) % 4 == 0, "activeDirectionalLights must be 4-byte aligned");
static_assert(offsetof(FragUbo, activeSpotLights) % 4 == 0, "activeSpotLights must be 4-byte aligned");

}

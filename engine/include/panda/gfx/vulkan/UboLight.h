#pragma once

#include "panda/gfx/Light.h"
#include "panda/gfx/vulkan/Alignment.h"

namespace panda::gfx::vulkan
{

struct UboAttenuation
{
    float constant;
    float linear;
    float exp;

    PD_MAKE_ALIGNED(constant, linear, exp)
};

static_assert(alignment(UboAttenuation {}) == 16);

struct UboBaseLight
{
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 diffuse;
    alignas(16) glm::vec3 specular;
    float intensity;

    PD_MAKE_ALIGNED(ambient, diffuse, specular, intensity)
};

static_assert(alignment(UboBaseLight {}) == 16);

struct UboDirectionalLight
{
    UboBaseLight base;
    alignas(16) glm::vec3 direction;

    PD_MAKE_ALIGNED(base, direction)
};

struct UboPointLight
{
    UboBaseLight base;
    alignas(16) UboAttenuation attenuation;
    alignas(16) glm::vec3 position;

    PD_MAKE_ALIGNED(base, attenuation, position)
};

struct UboSpotLight
{
    UboPointLight base;
    alignas(16) glm::vec3 direction;
    float cutOff;

    PD_MAKE_ALIGNED(base, direction, cutOff)
};

struct UboMaterial
{
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 diffuse;
    alignas(16) glm::vec3 specular;
    float shininess;

    PD_MAKE_ALIGNED(ambient, diffuse, specular, shininess)
};

constexpr auto fromDirectionalLight(const DirectionalLight& light) noexcept -> UboDirectionalLight
{
    return {
        {light.ambient, light.diffuse, light.specular, light.intensity},
        light.direction
    };
}

constexpr auto fromPointLight(const PointLight& light) noexcept -> UboPointLight
{
    return {
        {light.ambient, light.diffuse, light.specular, light.intensity},
        {light.attenuation.constant, light.attenuation.linear, light.attenuation.exp},
        light.position
    };
}

constexpr auto fromSpotLight(const SpotLight& light) noexcept -> UboSpotLight
{
    return {
        {{light.ambient, light.diffuse, light.specular, light.intensity},
         {light.attenuation.constant, light.attenuation.linear, light.attenuation.exp},
         light.position},
        light.direction,
        light.cutOff
    };
}

}

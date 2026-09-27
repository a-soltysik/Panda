#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <glm/ext/vector_float3.hpp>

#include "drip/gfx/scene/Light.hpp"
#include "vulkan/memory/Alignment.hpp"

namespace drip::gfx
{

struct UboAttenuation
{
    DRIP_ALIGNED_MEMBERS((float, constant),  //
                         (float, linear),    //
                         (float, exp))
};

static_assert(UboAttenuation::alignment() == 4);
static_assert(sizeof(UboAttenuation) == 3 * sizeof(float));
static_assert(offsetof(UboAttenuation, constant) == 0);
static_assert(offsetof(UboAttenuation, linear) == sizeof(float));
static_assert(offsetof(UboAttenuation, exp) == 2 * sizeof(float));

struct UboBaseLight
{
    DRIP_ALIGNED_MEMBERS((glm::vec3, ambient),   //
                         (glm::vec3, diffuse),   //
                         (glm::vec3, specular),  //
                         (float, intensity))
};

static_assert(UboBaseLight::alignment() == 16);
static_assert(offsetof(UboBaseLight, ambient) % 16 == 0);
static_assert(offsetof(UboBaseLight, diffuse) % 16 == 0);
static_assert(offsetof(UboBaseLight, specular) % 16 == 0);

struct UboDirectionalLight
{
    DRIP_ALIGNED_MEMBERS((UboBaseLight, base),  //
                         (glm::vec3, direction))
};

static_assert(UboDirectionalLight::alignment() == 16);
static_assert(offsetof(UboDirectionalLight, base) == 0);
static_assert(offsetof(UboDirectionalLight, direction) % 16 == 0);

struct UboPointLight
{
    DRIP_ALIGNED_MEMBERS((UboBaseLight, base),           //
                         (UboAttenuation, attenuation),  //
                         (glm::vec3, position))
};

static_assert(UboPointLight::alignment() == 16);
static_assert(offsetof(UboPointLight, base) % 16 == 0);
static_assert(offsetof(UboPointLight, attenuation) % 16 == 0);
static_assert(offsetof(UboPointLight, position) % 16 == 0);

struct UboSpotLight
{
    DRIP_ALIGNED_MEMBERS((UboPointLight, base),   //
                         (glm::vec3, direction),  //
                         (float, cutOff))
};

static_assert(UboSpotLight::alignment() == 16, "UboSpotLight alignment");
static_assert(offsetof(UboSpotLight, base) == 0, "base at offset 0");
static_assert(offsetof(UboSpotLight, direction) % 16 == 0, "direction must be 16-byte aligned");

constexpr auto fromDirectionalLight(const DirectionalLight& light) noexcept -> UboDirectionalLight
{
    return {
        .base = {.ambient = light.ambient,
                 .diffuse = light.diffuse,
                 .specular = light.specular,
                 .intensity = light.intensity},
        .direction = light.direction
    };
}

constexpr auto fromPointLight(const PointLight& light) noexcept -> UboPointLight
{
    return {
        .base = {.ambient = light.ambient,
                 .diffuse = light.diffuse,
                 .specular = light.specular,
                 .intensity = light.intensity},
        .attenuation = {.constant = light.attenuation.constant,
                 .linear = light.attenuation.linear,
                 .exp = light.attenuation.exp},
        .position = light.position
    };
}

constexpr auto fromSpotLight(const SpotLight& light) noexcept -> UboSpotLight
{
    return {
        .base = {.base = {.ambient = light.ambient,
                          .diffuse = light.diffuse,
                          .specular = light.specular,
                          .intensity = light.intensity},
                 .attenuation = {.constant = light.attenuation.constant,
                                 .linear = light.attenuation.linear,
                                 .exp = light.attenuation.exp},
                 .position = light.position},
        .direction = light.direction,
        .cutOff = light.cutOff
    };
}

}

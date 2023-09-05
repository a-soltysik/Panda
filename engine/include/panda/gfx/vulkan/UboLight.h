#pragma once

#include <algorithm>

#include "panda/gfx/Light.h"
#include "panda/gfx/vulkan/Alignment.h"

namespace panda::gfx::vulkan
{

struct UboDirectionalLight
{
    glm::vec4 direction;
    glm::vec4 color;

    PD_MAKE_ALIGNED(direction, color)
};

constexpr auto fromDirectionalLight(const DirectionalLight& light) noexcept -> UboDirectionalLight
{
    return {
        {light.direction, 1.f            },
        {light.color,     light.intensity}
    };
}

static_assert(alignment(UboDirectionalLight {}) == 16);

struct UboPointLight
{
    glm::vec4 position;
    glm::vec4 color;

    PD_MAKE_ALIGNED(position, color)
};

constexpr auto fromPointLight(const PointLight& light) noexcept -> UboPointLight
{
    return {
        {light.position, 1.f            },
        {light.color,    light.intensity},
    };
}

static_assert(alignment(UboPointLight {}) == 16);

}

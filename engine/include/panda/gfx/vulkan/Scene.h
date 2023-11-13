#pragma once

#include <numeric>

#include "panda/gfx/Camera.h"
#include "panda/gfx/Light.h"
#include "panda/gfx/vulkan/object/Object.h"

namespace panda::gfx::vulkan
{

struct Scene
{
    Camera camera;
    std::vector<Object> objects;
    Lights lights;

    [[nodiscard]] auto getSize() const -> size_t
    {
        return std::accumulate(objects.begin(),
                               objects.end(),
                               size_t {},
                               [](auto current, const auto& object) {
                                   return current + object.surfaces.size();
                               }) +
               lights.spotLights.size() + lights.pointLights.size() + lights.directionalLights.size();
    }
};

}
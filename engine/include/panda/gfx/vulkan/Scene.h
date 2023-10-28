#pragma once

#include "panda/gfx/Camera.h"
#include "panda/gfx/vulkan/Object.h"

namespace panda::gfx::vulkan
{

struct Scene
{
    Camera camera;
    std::vector<Object> objects;
    Lights lights;
};

}
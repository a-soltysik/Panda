#pragma once

#include <panda/gfx/vulkan/Scene.h>

namespace app
{

class UserGui
{
public:
    auto render(panda::gfx::vulkan::Scene& scene) -> void;

private:
    int _currentObject = 0;
    panda::gfx::vulkan::Transform* _currentTransform {};
};

}

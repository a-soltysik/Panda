#include "App.h"

#include <iostream>

#include "gfx/api/vulkan/Vulkan.h"

namespace panda
{

auto App::run() -> int
{
    window = std::make_unique<Window>();
    api = std::make_unique<gfx::vulkan::Vulkan>();

    mainLoop();
    return 0;
}

auto App::mainLoop() -> void
{
    while (!window->shouldClose())
    {
        window->processInput();
        api->render();
    }
}

}
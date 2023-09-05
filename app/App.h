#pragma once

#include <memory>

#include "GlfwWindow.h"
#include <panda/gfx/vulkan/Context.h>

namespace app
{

class App
{
public:
    auto run() -> int;

private:
    static auto initializeLogger() -> void;
    static auto registerSignalHandlers() -> void;
    auto mainLoop() -> void;

    std::unique_ptr<GlfwWindow> _window;
    std::unique_ptr<panda::gfx::vulkan::Context> _api;
};

}

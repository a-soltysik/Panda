#pragma once

#include <memory>

#include "Window.h"
#include "gfx/api/RenderingApi.h"

namespace panda
{

class App
{
public:
    auto run() -> int;

private:
    static auto initializeLogger() -> void;
    static auto registerSignalHandlers() -> void;
    auto mainLoop() -> void;

    std::unique_ptr<Window> _window;
    std::unique_ptr<gfx::RenderingApi> _api;
};

}

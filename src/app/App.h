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
    auto mainLoop() -> void;
    auto initializeLogger() -> void;

    std::unique_ptr<Window> window;
    std::unique_ptr<gfx::RenderingApi> api;
};

}

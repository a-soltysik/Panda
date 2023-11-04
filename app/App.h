#pragma once

#include <panda/gfx/vulkan/Context.h>

#include <memory>

#include "GlfwWindow.h"

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
    auto setObjects(panda::gfx::vulkan::Scene& scene) -> void;

    std::unique_ptr<GlfwWindow> _window;
    std::unique_ptr<panda::gfx::vulkan::Context> _api;
    std::unique_ptr<panda::gfx::vulkan::Mesh> _floorMesh;
    std::unique_ptr<panda::gfx::vulkan::Mesh> _vaseMesh;
};

}

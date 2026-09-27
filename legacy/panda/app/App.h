#pragma once

#include <panda/gfx/vulkan/Context.h>
#include <panda/gfx/vulkan/Scene.h>

#include <memory>

#include "GlfwWindow.h"
#include "utils/Signals.h"

namespace app
{

class App
{
public:
    App();
    auto run() -> int;

private:
    static auto initializeLogger() -> void;
    static auto registerSignalHandlers() -> void;
    auto mainLoop() -> void;
    auto setDefaultScene() -> void;

    panda::gfx::vulkan::Scene _scene {};

    utils::signals::NewMeshAdded::ReceiverT _newMeshAddedReceiver;
    std::unique_ptr<GlfwWindow> _window;
    std::unique_ptr<panda::gfx::vulkan::Context> _api;
};

}

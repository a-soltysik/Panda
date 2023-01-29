#pragma once

#include <memory>

#include "gfx/api/Api.h"

namespace panda
{

class App
{
public:
    App();
    ~App() noexcept;
    App(const App&) = delete;
    App(App&&) noexcept = default;
    auto operator=(const App&) -> App& = delete;
    auto operator=(App&&) noexcept -> App& = default;

    auto run() -> int;

private:
    auto initWindow() -> bool;
    auto mainLoop() -> void;
    auto cleanup() const -> void;

    std::unique_ptr<gfx::Api> api;
    GLFWwindow* window = nullptr;
    bool glfwInitialized = false;
};

}

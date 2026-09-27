#pragma once

#include <panda/tools/gui/Window.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <expected>
#include <memory>
#include <thread>

namespace panda::tools
{
class GlfwSession final
{
public:
    GlfwSession() = default;

    GlfwSession(const GlfwSession&) = delete;

    GlfwSession(GlfwSession&&) = delete;

    auto operator=(const GlfwSession&) -> GlfwSession& = delete;

    auto operator=(GlfwSession&&) -> GlfwSession& = delete;

    ~GlfwSession() noexcept;

    void checkThread() const;

    static auto cache() -> std::weak_ptr<GlfwSession>&;

    static auto acquire() -> std::expected<std::shared_ptr<GlfwSession>, WindowError>;

private:
    std::thread::id _owner {std::this_thread::get_id()};
    GLFWerrorfun _previousCallback {nullptr};
    bool _initialized {false};
};
}

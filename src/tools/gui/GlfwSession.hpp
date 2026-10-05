#pragma once

#include <panda/tools/gui/Window.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <memory>
#include <source_location>
#include <thread>

namespace panda::tools
{
class GlfwSession final
{
public:
    static void clearError();

    [[nodiscard]] static auto takeError(std::source_location source = std::source_location::current()) -> Error;

    [[nodiscard]] static auto checkError(std::source_location source = std::source_location::current()) -> Result<void>;

    static auto cache() -> std::weak_ptr<GlfwSession>&;

    static auto acquire(std::source_location source = std::source_location::current())
        -> Result<std::shared_ptr<GlfwSession>>;

    GlfwSession() = default;

    GlfwSession(const GlfwSession&) = delete;

    GlfwSession(GlfwSession&&) = delete;

    auto operator=(const GlfwSession&) -> GlfwSession& = delete;

    auto operator=(GlfwSession&&) -> GlfwSession& = delete;

    ~GlfwSession() noexcept;

    void checkThread() const;

private:
    std::thread::id _owner {std::this_thread::get_id()};
    GLFWerrorfun _previousCallback {nullptr};
    bool _initialized {false};
};
}

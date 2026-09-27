#include "GlfwSession.hpp"

#include <GLFW/glfw3.h>

#include <expected>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <thread>
#include <tuple>

namespace panda::tools
{
namespace
{
void reportGlfwError(int code, const char* description) noexcept
{
    try
    {
        log::error("GLFW error {}: {}", code, description != nullptr ? description : "No description");
    }
    catch (...)
    {
        panic("Could not report a GLFW error");
    }
}
}

void GlfwSession::checkThread() const
{
    expect(_owner == std::this_thread::get_id(), "Window operation requires the owning main thread");
}

GlfwSession::~GlfwSession() noexcept
{
    checkThread();
    if (_initialized)
    {
        glfwTerminate();
    }
    std::ignore = glfwSetErrorCallback(_previousCallback);
}

auto GlfwSession::cache() -> std::weak_ptr<GlfwSession>&
{
    static std::weak_ptr<GlfwSession> current;
    return current;
}

auto GlfwSession::acquire() -> std::expected<std::shared_ptr<GlfwSession>, WindowError>
{
    if (auto current {cache().lock()})
    {
        current->checkThread();
        return current;
    }
    auto session {std::make_shared<GlfwSession>()};
    session->_previousCallback = glfwSetErrorCallback(reportGlfwError);
    std::ignore = glfwGetError(nullptr);
    if (glfwInit() != GLFW_TRUE)
    {
        const char* description {nullptr};
        const auto nativeCode {glfwGetError(&description)};
        return std::unexpected {
            WindowError {.code = WindowError::Code::InitializationFailed,
                         .operation = "glfwInit",
                         .message = description != nullptr ? description : "GLFW failed without an error description",
                         .nativeCode = nativeCode}
        };
    }
    session->_initialized = true;
    cache() = session;
    log::debug("GLFW session initialized");
    return session;
}
}

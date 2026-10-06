#include "GlfwSession.hpp"

#include <GLFW/glfw3.h>

#include <expected>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <source_location>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

namespace panda::tools
{
namespace
{
auto glfwErrorCode(int code) -> ErrorCode
{
    switch (code)
    {
    case GLFW_API_UNAVAILABLE:
    case GLFW_FORMAT_UNAVAILABLE:
    case GLFW_PLATFORM_UNAVAILABLE:
    case GLFW_VERSION_UNAVAILABLE:
        return ErrorCode::Unsupported;
    case GLFW_OUT_OF_MEMORY:
        return ErrorCode::ResourceExhausted;
    default:
        return ErrorCode::BackendFailure;
    }
}

auto makeGlfwError(int code, const char* description, std::source_location source) -> Error
{
    auto native = std::optional<NativeError> {};
    if (code != GLFW_NO_ERROR)
    {
        native = NativeError {.api = "GLFW", .code = code};
    }
    return makeError(glfwErrorCode(code),
                     description != nullptr ? description : "GLFW failed without an error description",
                     std::move(native),
                     source);
}

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
    static auto current = std::weak_ptr<GlfwSession> {};
    return current;
}

void GlfwSession::clearError()
{
    std::ignore = glfwGetError(nullptr);
}

auto GlfwSession::takeError(std::source_location source) -> Error
{
    const auto* description = static_cast<const char*>(nullptr);
    const auto code = glfwGetError(&description);
    return makeGlfwError(code, description, source);
}

auto GlfwSession::checkError(std::source_location source) -> Result<void>
{
    const auto* description = static_cast<const char*>(nullptr);
    const auto code = glfwGetError(&description);
    if (code != GLFW_NO_ERROR)
    {
        return std::unexpected {makeGlfwError(code, description, source)};
    }
    return {};
}

auto GlfwSession::acquire(std::source_location source) -> Result<std::shared_ptr<GlfwSession>>
{
    if (auto current {cache().lock()})
    {
        current->checkThread();
        return current;
    }
    auto session = std::make_shared<GlfwSession>();
    session->_previousCallback = glfwSetErrorCallback(reportGlfwError);
    clearError();
    if (glfwInit() != GLFW_TRUE)
    {
        return std::unexpected {takeError(source)};
    }
    session->_initialized = true;
    cache() = session;
    log::debug("GLFW session initialized");
    return session;
}
}

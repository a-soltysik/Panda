#include "panda/tools/gui/Window.hpp"

#include "GlfwSession.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <bit>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Logger.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace panda::tools
{
namespace
{
auto takeError(WindowError::Code category, std::string_view operation) -> WindowError
{
    const auto* description = static_cast<const char*>(nullptr);
    const auto nativeCode = glfwGetError(&description);
    return {.code = category,
            .operation = std::string {operation},
            .message = description != nullptr ? description : "GLFW failed without an error description",
            .nativeCode = nativeCode};
}

auto glfwResult(std::string_view operation) -> std::expected<void, WindowError>
{
    const auto* description = static_cast<const char*>(nullptr);
    const auto code = glfwGetError(&description);
    if (code != GLFW_NO_ERROR)
    {
        return std::unexpected {
            WindowError {.code = WindowError::Code::OperationFailed,
                         .operation = std::string {operation},
                         .message = description != nullptr ? description : "No description",
                         .nativeCode = code}
        };
    }
    return {};
}

void clearError()
{
    std::ignore = glfwGetError(nullptr);
}
}

Window::Window(GLFWwindow* window, std::shared_ptr<GlfwSession> session) noexcept
    : _window {window},
      _session {std::move(session)}
{
}

Window::Window(Window&& other) noexcept
    : _window {std::exchange(other._window, nullptr)},
      _session {std::move(other._session)}
{
    if (_window != nullptr)
    {
        checkUsable();
    }
}

Window::~Window() noexcept
{
    if (_window != nullptr)
    {
        checkUsable();
        glfwDestroyWindow(_window);
    }
}

auto Window::create(glm::uvec2 size, const char* name) -> std::expected<Window, WindowError>
{
    static constexpr auto maximumDimension = static_cast<unsigned int>(std::numeric_limits<int>::max());
    if (name == nullptr || size.x == 0 || size.y == 0 || size.x > maximumDimension || size.y > maximumDimension)
    {
        static constexpr auto message =
            std::string_view {"Window requires a non-null title and positive dimensions within INT_MAX"};
        log::warning("{}", message);
        return std::unexpected {
            WindowError {.code = WindowError::Code::InvalidArgument,
                         .operation = "Window::create",
                         .message = std::string {message}}
        };
    }
    auto acquired = GlfwSession::acquire();
    if (!acquired)
    {
        return std::unexpected {std::move(acquired.error())};
    }
    auto session = *std::move(acquired);
    clearError();
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (auto configured = glfwResult("Window hints"); !configured)
    {
        return std::unexpected {std::move(configured.error())};
    }
    auto* const nativeWindow =
        glfwCreateWindow(static_cast<int>(size.x), static_cast<int>(size.y), name, nullptr, nullptr);
    if (nativeWindow == nullptr)
    {
        return std::unexpected {takeError(WindowError::Code::CreationFailed, "glfwCreateWindow")};
    }
    auto window = Window {nativeWindow, std::move(session)};
    log::info("Window '{}' created, {}x{}", name, size.x, size.y);
    return window;
}

auto Window::isValid() const noexcept -> bool
{
    return _window != nullptr;
}

void Window::checkUsable() const
{
    expect(_window != nullptr && _session != nullptr, "Cannot use a moved-from Window");
    _session->checkThread();
}

auto Window::shouldClose() const -> bool
{
    checkUsable();
    clearError();
    const auto closed = glfwWindowShouldClose(_window) == GLFW_TRUE;
    expect(glfwResult("glfwWindowShouldClose"), "GLFW window ownership is invalid");
    return closed;
}

auto Window::processInput() -> std::expected<void, WindowError>
{
    checkUsable();
    clearError();
    glfwPollEvents();
    return glfwResult("glfwPollEvents");
}

auto Window::getSize() const -> std::expected<glm::uvec2, WindowError>
{
    checkUsable();
    clearError();
    auto width = 0;
    auto height = 0;
    glfwGetWindowSize(_window, &width, &height);
    if (auto result = glfwResult("glfwGetWindowSize"); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    if (width < 0 || height < 0)
    {
        return std::unexpected {
            WindowError {.code = WindowError::Code::OperationFailed,
                         .operation = "glfwGetWindowSize",
                         .message = "GLFW reported negative window dimensions"}
        };
    }
    return glm::uvec2 {static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
}

auto Window::isMinimized() const -> std::expected<bool, WindowError>
{
    checkUsable();
    clearError();
    auto width = 0;
    auto height = 0;
    glfwGetFramebufferSize(_window, &width, &height);
    if (auto result = glfwResult("glfwGetFramebufferSize"); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    if (width < 0 || height < 0)
    {
        return std::unexpected {
            WindowError {.code = WindowError::Code::OperationFailed,
                         .operation = "glfwGetFramebufferSize",
                         .message = "GLFW reported negative framebuffer dimensions"}
        };
    }
    const auto iconified = glfwGetWindowAttrib(_window, GLFW_ICONIFIED) == GLFW_TRUE;
    if (auto result = glfwResult("glfwGetWindowAttrib"); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    return iconified || width == 0 || height == 0;
}

auto Window::waitForInput() -> std::expected<void, WindowError>
{
    const auto session = GlfwSession::cache().lock();
    expect(session != nullptr, "Waiting for input requires a live Panda window");
    session->checkThread();
    clearError();
    glfwWaitEvents();
    return glfwResult("glfwWaitEvents");
}

auto Window::getId() const -> Id
{
    checkUsable();
    return std::bit_cast<Id>(_window);
}

}

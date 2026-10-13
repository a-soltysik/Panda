#include "panda/tools/gui/Window.hpp"

#include "GlfwSession.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include <bit>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace panda::tools
{
namespace
{
auto validateWindowRequest(glm::uvec2 size, const char* name) -> Result<void>
{
    static constexpr auto maximumDimension = static_cast<unsigned int>(std::numeric_limits<int>::max());
    if (name == nullptr || size.x == 0 || size.y == 0 || size.x > maximumDimension || size.y > maximumDimension)
    {
        static constexpr auto message =
            std::string_view {"Window requires a non-null title and positive dimensions within INT_MAX"};
        log::warning("{}", message);
        return std::unexpected {makeError(ErrorCode::InvalidArgument, std::string {message})};
    }
    return {};
}

auto createNativeWindow(glm::uvec2 size, const char* name, std::source_location source) -> Result<GLFWwindow*>
{
    GlfwSession::clearError();
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (auto configured = GlfwSession::checkError(source); !configured)
    {
        return std::unexpected {std::move(configured.error())};
    }
    auto* const window = glfwCreateWindow(static_cast<int>(size.x), static_cast<int>(size.y), name, nullptr, nullptr);
    if (window == nullptr)
    {
        return std::unexpected {GlfwSession::takeError(source)};
    }
    return window;
}

auto readFramebufferSize(GLFWwindow* window) -> Result<FramebufferExtent>
{
    GlfwSession::clearError();
    auto width = 0;
    auto height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (auto result = GlfwSession::checkError(); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    if (width < 0 || height < 0)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "GLFW reported negative framebuffer dimensions")};
    }
    return FramebufferExtent {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height)};
}

auto isWindowIconified(GLFWwindow* window) -> Result<bool>
{
    GlfwSession::clearError();
    const auto iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
    if (auto result = GlfwSession::checkError(); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    return iconified;
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

auto Window::create(glm::uvec2 size, const char* name) -> Result<Window>
{
    if (const auto valid = validateWindowRequest(size, name); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto acquired = GlfwSession::acquire();
    if (!acquired)
    {
        return std::unexpected {std::move(acquired.error())};
    }
    auto session = *std::move(acquired);
    auto nativeWindow = createNativeWindow(size, name, std::source_location::current());
    if (!nativeWindow)
    {
        return std::unexpected {std::move(nativeWindow.error())};
    }
    auto window = Window {*nativeWindow, std::move(session)};
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
    GlfwSession::clearError();
    const auto closed = glfwWindowShouldClose(_window) == GLFW_TRUE;
    expect(GlfwSession::checkError(), "GLFW window ownership is invalid");
    return closed;
}

auto Window::processInput() const -> Result<void>
{
    checkUsable();
    GlfwSession::clearError();
    glfwPollEvents();
    return GlfwSession::checkError();
}

auto Window::getSize() const -> Result<glm::uvec2>
{
    checkUsable();
    GlfwSession::clearError();
    auto width = 0;
    auto height = 0;
    glfwGetWindowSize(_window, &width, &height);
    if (auto result = GlfwSession::checkError(); !result)
    {
        return std::unexpected {std::move(result.error())};
    }
    if (width < 0 || height < 0)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "GLFW reported negative window dimensions")};
    }
    return glm::uvec2 {static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
}

auto Window::setSize(glm::uvec2 size) -> Result<void>
{
    checkUsable();
    static constexpr auto maximumDimension = static_cast<unsigned int>(std::numeric_limits<int>::max());
    if (size.x == 0 || size.y == 0 || size.x > maximumDimension || size.y > maximumDimension)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Window size must be positive and within INT_MAX")};
    }
    GlfwSession::clearError();
    glfwSetWindowSize(_window, static_cast<int>(size.x), static_cast<int>(size.y));
    return GlfwSession::checkError();
}

auto Window::setMinimized(bool minimized) -> Result<void>
{
    checkUsable();
    GlfwSession::clearError();
    if (minimized)
    {
        glfwIconifyWindow(_window);
    }
    else
    {
        glfwRestoreWindow(_window);
    }
    return GlfwSession::checkError();
}

auto Window::isMinimized() const -> Result<bool>
{
    const auto extent = getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    return extent->width == 0 || extent->height == 0;
}

auto Window::getRequiredInstanceExtensions() const -> Result<std::vector<std::string>>
{
    checkUsable();
    GlfwSession::clearError();
    auto count = std::uint32_t {0};
    const auto* names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr || count == 0)
    {
        return std::unexpected {GlfwSession::takeError()};
    }
    return std::span {names, count} | std::ranges::to<std::vector<std::string>>();
}

auto Window::createSurface(vk::Instance instance) const -> Result<vk::SurfaceKHR>
{
    checkUsable();
    if (!instance)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Vulkan instance is null")};
    }
    GlfwSession::clearError();
    auto* surface = VkSurfaceKHR {};
    const auto result = static_cast<vk::Result>(glfwCreateWindowSurface(instance, _window, nullptr, &surface));
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {panda::makeVulkanError(result)};
    }
    return surface;
}

auto Window::getFramebufferExtent() const -> Result<FramebufferExtent>
{
    checkUsable();
    const auto extent = readFramebufferSize(_window);
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    const auto iconified = isWindowIconified(_window);
    if (!iconified)
    {
        return std::unexpected {iconified.error()};
    }
    return *iconified ? FramebufferExtent {} : *extent;
}

auto Window::waitForInput() -> Result<void>
{
    const auto session = GlfwSession::cache().lock();
    expect(session != nullptr, "Waiting for input requires a live Panda window");
    session->checkThread();
    GlfwSession::clearError();
    glfwWaitEvents();
    return GlfwSession::checkError();
}

auto Window::getId() const -> Id
{
    checkUsable();
    return std::bit_cast<Id>(_window);
}

}

#include "Window.h"

namespace
{

void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    static auto sender = panda::utils::Signals::frameBufferResized.registerSender();
    panda::log::Info("Size of window [{}] changed to {}x{}", static_cast<void*>(window), width, height);

    sender(width, height);
}

}

namespace panda
{

Window::Window(glm::uvec2 size, const char* name)
    : _window {createWindow(size, name)},
      _size {size}
{
    glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);

    _frameBufferResizedReceiver = utils::Signals::frameBufferResized.connect([this](int x, int y) {
        _size = {x, y};
    });
}

Window::~Window() noexcept
{
    glfwDestroyWindow(_window);
    glfwTerminate();
    log::Info("Window [{}] destroyed", static_cast<void*>(_window));
}

auto Window::createWindow(glm::uvec2 size, const char* name) -> GLFWwindow*
{
    expect(glfwInit(), GLFW_TRUE, "Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* window = glfwCreateWindow(static_cast<int>(size.x), static_cast<int>(size.y), name, nullptr, nullptr);
    log::Info("Window [{}] {}x{} px created", static_cast<void*>(window), size.x, size.y);

    return window;
}

auto Window::getHandle() const noexcept -> GLFWwindow*
{
    return _window;
}

auto Window::shouldClose() const noexcept -> bool
{
    return glfwWindowShouldClose(_window) == GLFW_TRUE;
}

auto Window::processInput() noexcept -> void
{
    glfwPollEvents();
}

auto Window::getSize() const noexcept -> glm::uvec2
{
    return _size;
}

auto Window::isMinimized() const noexcept -> bool
{
    return _size.x == 0 || _size.y == 0;
}

auto Window::waitForInput() noexcept -> void
{
    glfwWaitEvents();
}

}

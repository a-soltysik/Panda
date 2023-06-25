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

Window::Window(glm::uvec2 initialSize, const char* name)
    : window {createWindow(initialSize, name)},
      size {initialSize}
{
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    frameBufferResizedReceiver = utils::Signals::frameBufferResized.connect([this](int x, int y) {
        size = {x, y};
    });
}

Window::~Window() noexcept
{
    glfwDestroyWindow(window);
    glfwTerminate();
    log::Info("Window [{}] destroyed", static_cast<void*>(window));
}

auto Window::createWindow(glm::uvec2 initialSize, const char* name) -> GLFWwindow*
{
    expect(glfwInit(), GLFW_TRUE, "Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* window =
        glfwCreateWindow(static_cast<int>(initialSize.x), static_cast<int>(initialSize.y), name, nullptr, nullptr);
    log::Info("Window [{}] {}x{} px created", static_cast<void*>(window), initialSize.x, initialSize.y);

    return window;
}

auto Window::getHandle() const noexcept -> GLFWwindow*
{
    return window;
}

auto Window::shouldClose() const noexcept -> bool
{
    return glfwWindowShouldClose(window) == GLFW_TRUE;
}

auto Window::processInput() noexcept -> void
{
    glfwPollEvents();
}

auto Window::getSize() const noexcept -> glm::uvec2
{
    return size;
}

auto Window::isMinimized() const noexcept -> bool
{
    return size.x == 0 || size.y == 0;
}

auto Window::waitForInput() noexcept -> void
{
    glfwWaitEvents();
}

}

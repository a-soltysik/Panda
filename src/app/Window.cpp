#include "Window.h"

namespace panda
{

void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    log::Info("Size of window [{}] changed to {}x{}", static_cast<void*>(window), width, height);

    auto* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));

    if (instance == nullptr)
    {
        log::Warning("Instance of the window [{}] doesn't exist", static_cast<void*>(window));
        return;
    }

    instance->frameBufferResizeSender.send(width, height);
}

Window::Window(glm::uvec2 initialSize, const char* name)
{
    expect(glfwInit(), GLFW_TRUE, "Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(static_cast<int>(initialSize.x), static_cast<int>(initialSize.y), name, nullptr, nullptr);
    log::Info("Window [{}] {}x{} px created", static_cast<void*>(window), initialSize.x, initialSize.y);

    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetWindowUserPointer(window, this);
}

Window::~Window() noexcept
{
    glfwDestroyWindow(window);
    glfwTerminate();
    log::Info("Window [{}] destroyed", static_cast<void*>(window));
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
    auto width = int {};
    auto height = int {};
    glfwGetFramebufferSize(window, &width, &height);

    return {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
    };
}

auto Window::isMinimized() const noexcept -> bool
{
    const auto size = getSize();
    return size.x == 0 || size.y == 0;
}

auto Window::waitForInput() noexcept -> void
{
    glfwWaitEvents();
}

auto Window::getFrameBufferResizeSignal() const noexcept -> utils::Signal<FrameBufferResize>&
{
    return frameBufferResizeSender.getSignal();
}

}

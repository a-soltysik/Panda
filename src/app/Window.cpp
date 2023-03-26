#include "Window.h"

namespace panda
{

void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    log::Info("Size of window [{}] changed to {}x{}", static_cast<void*>(window), width, height);

    const auto* instance = static_cast<Window*>(glfwGetWindowUserPointer(window));

    if (instance == nullptr)
    {
        log::Warning("Instance of the window [{}] doesn't exist", static_cast<void*>(window));
        return;
    }

    for (const auto& action : instance->frameBufferResizeSubscribers)
    {
        action(width, height);
    }
}

Window::Window()
{
    if (glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("Can't initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    static constexpr auto width = int {800};
    static constexpr auto height = int {600};

    window = glfwCreateWindow(width, height, ENGINE_TARGET_NAME, nullptr, nullptr);
    log::Info("Window [{}] {}x{} px created", static_cast<void*>(window), width, height);

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

auto Window::processInput() const noexcept -> void
{
    glfwPollEvents();
}

auto Window::subscribeForFrameBufferResize(std::function<void(int, int)>&& action) -> void
{
    frameBufferResizeSubscribers.push_back(std::move(action));
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

auto Window::waitForInput() const noexcept -> void
{
    glfwWaitEvents();
}

}

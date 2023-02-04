#include "Window.h"

namespace panda
{

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

    window = glfwCreateWindow(width, height, TARGET_NAME, nullptr, nullptr);
}

Window::~Window() noexcept
{
    glfwDestroyWindow(window);
    glfwTerminate();
}

auto Window::getHandle() const noexcept -> GLFWwindow*
{
    return window;
}

auto Window::shouldClose() const noexcept -> bool
{
    return glfwWindowShouldClose(window) == GLFW_TRUE;
}

auto Window::processInput() const noexcept -> void {
    glfwPollEvents();
}

}

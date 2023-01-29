#include "App.h"

#include <algorithm>
#include <iostream>

#include "gfx/api/vulkan/Vulkan.h"

namespace panda
{

App::App()
    : api {std::make_unique<gfx::vk::Vulkan>()}
{
}

App::~App() noexcept
{
    cleanup();
}

auto App::run() -> int
{
    if (!initWindow())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    if (!api->init())
    {
        std::cerr << "Failed to create api Instance\n";
        return -1;
    }
    mainLoop();
    return 0;
}

auto App::initWindow() -> bool
{
    if (glfwInit() != GLFW_TRUE)
    {
        return false;
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    static constexpr auto width = int {800};
    static constexpr auto height = int {600};

    window = glfwCreateWindow(width, height, PROJECT_NAME, nullptr, nullptr);

    return window != nullptr;
}

auto App::mainLoop() -> void
{
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
    }
}

auto App::cleanup() const -> void
{
    api->cleanup();
    if (glfwInitialized)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}

}
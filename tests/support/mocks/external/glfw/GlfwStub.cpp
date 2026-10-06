#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>

#include "GlfwMock.hpp"
#include "ScopedMock.hpp"

extern "C" {
auto glfwInit() -> int
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwInit();
}

void glfwTerminate()
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwTerminate();
}

auto glfwSetErrorCallback(GLFWerrorfun callback) -> GLFWerrorfun
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwSetErrorCallback(callback);
}

auto glfwGetError(const char** description) -> int
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwGetError(description);
}

void glfwDefaultWindowHints()
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwDefaultWindowHints();
}

void glfwWindowHint(int hint, int value)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwWindowHint(hint, value);
}

auto glfwCreateWindow(int width, int height, const char* title, GLFWmonitor* monitor, GLFWwindow* share) -> GLFWwindow*
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwCreateWindow(width,
                                                                                            height,
                                                                                            title,
                                                                                            monitor,
                                                                                            share);
}

void glfwDestroyWindow(GLFWwindow* window)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwDestroyWindow(window);
}

auto glfwWindowShouldClose(GLFWwindow* window) -> int
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwWindowShouldClose(window);
}

void glfwGetWindowSize(GLFWwindow* window, int* width, int* height)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwGetWindowSize(window, width, height);
}

void glfwSetWindowSize(GLFWwindow* window, int width, int height)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwSetWindowSize(window, width, height);
}

void glfwIconifyWindow(GLFWwindow* window)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwIconifyWindow(window);
}

void glfwRestoreWindow(GLFWwindow* window)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwRestoreWindow(window);
}

void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height)
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwGetFramebufferSize(window, width, height);
}

auto glfwGetWindowAttrib(GLFWwindow* window, int attribute) -> int
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwGetWindowAttrib(window, attribute);
}

void glfwPollEvents()
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwPollEvents();
}

void glfwWaitEvents()
{
    panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwWaitEvents();
}

auto glfwGetRequiredInstanceExtensions(std::uint32_t* count) -> const char**
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwGetRequiredInstanceExtensions(count);
}

auto glfwCreateWindowSurface(VkInstance instance,
                             GLFWwindow* window,
                             const VkAllocationCallbacks* allocator,
                             VkSurfaceKHR* surface) -> VkResult
{
    return panda::test::ScopedMock<panda::test::GlfwMock>::getActiveMock().glfwCreateWindowSurface(instance,
                                                                                                   window,
                                                                                                   allocator,
                                                                                                   surface);
}
}

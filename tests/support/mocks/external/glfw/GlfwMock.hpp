#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gmock/gmock.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>

// Opaque identity tokens only: no simulated window behavior or ownership.
struct GLFWwindow
{
};

namespace panda::test
{
class GlfwMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(int, glfwInit, (), ());

    MOCK_METHOD(void, glfwTerminate, (), ());

    MOCK_METHOD(GLFWerrorfun, glfwSetErrorCallback, (GLFWerrorfun), ());

    MOCK_METHOD(int, glfwGetError, (const char**), ());

    MOCK_METHOD(void, glfwDefaultWindowHints, (), ());

    MOCK_METHOD(void, glfwWindowHint, (int, int), ());

    MOCK_METHOD(GLFWwindow*, glfwCreateWindow, (int, int, const char*, GLFWmonitor*, GLFWwindow*), ());

    MOCK_METHOD(void, glfwDestroyWindow, (GLFWwindow*), ());

    MOCK_METHOD(int, glfwWindowShouldClose, (GLFWwindow*), ());

    MOCK_METHOD(void, glfwGetWindowSize, (GLFWwindow*, int*, int*), ());

    MOCK_METHOD(void, glfwSetWindowSize, (GLFWwindow*, int, int), ());

    MOCK_METHOD(void, glfwIconifyWindow, (GLFWwindow*), ());

    MOCK_METHOD(void, glfwRestoreWindow, (GLFWwindow*), ());

    MOCK_METHOD(void, glfwGetFramebufferSize, (GLFWwindow*, int*, int*), ());

    MOCK_METHOD(int, glfwGetWindowAttrib, (GLFWwindow*, int), ());

    MOCK_METHOD(void, glfwPollEvents, (), ());

    MOCK_METHOD(void, glfwWaitEvents, (), ());

    MOCK_METHOD(const char**, glfwGetRequiredInstanceExtensions, (std::uint32_t*), ());

    MOCK_METHOD(VkResult,
                glfwCreateWindowSurface,
                (VkInstance, GLFWwindow*, const VkAllocationCallbacks*, VkSurfaceKHR*),
                ());
    // NOLINTEND(modernize-use-trailing-return-type)
};

}

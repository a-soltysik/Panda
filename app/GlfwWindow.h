#pragma once

#include <GLFW/glfw3.h>
#include <panda/Window.h>
#include <panda/utils/Signals.h>
#include <vulkan/vulkan_core.h>

#include <glm/ext/vector_uint2.hpp>
#include <memory>
#include <vector>

namespace app
{

class KeyboardHandler;
class MouseHandler;

class GlfwWindow : public panda::Window
{
public:
    [[nodiscard]] static auto makeId(GLFWwindow* window) -> Id;

    GlfwWindow(glm::uvec2 size, const char* name);
    GlfwWindow(const GlfwWindow&) = delete;
    auto operator=(const GlfwWindow&) -> GlfwWindow& = delete;
    GlfwWindow(GlfwWindow&&) noexcept;
    auto operator=(GlfwWindow&&) -> GlfwWindow& = delete;
    ~GlfwWindow() noexcept override;

    [[nodiscard]] auto shouldClose() const -> bool override;
    [[nodiscard]] auto isMinimized() const -> bool override;
    [[nodiscard]] auto getSize() const -> glm::uvec2 override;
    [[nodiscard]] auto getRequiredExtensions() const -> std::vector<const char*> override;
    [[nodiscard]] auto createSurface(VkInstance instance) const -> VkSurfaceKHR override;
    [[nodiscard]] auto getId() const -> Id override;
    auto processInput() -> void override;
    auto waitForInput() -> void override;

    auto setKeyCallback(GLFWkeyfun callback) const noexcept -> GLFWkeyfun;
    auto setMouseButtonCallback(GLFWmousebuttonfun callback) const noexcept -> GLFWmousebuttonfun;
    auto setCursorPositionCallback(GLFWcursorposfun callback) const noexcept -> GLFWcursorposfun;

    [[nodiscard]] auto getKeyboardHandler() const noexcept -> const KeyboardHandler&;
    [[nodiscard]] auto getMouseHandler() const noexcept -> const MouseHandler&;

private:
    [[nodiscard]] static auto createWindow(glm::uvec2 size, const char* name) -> GLFWwindow*;
    auto setupImGui() const -> void;

    std::unique_ptr<KeyboardHandler> _keyboardHandler;
    std::unique_ptr<MouseHandler> _mouseHandler;
    panda::utils::signals::FrameBufferResized::ReceiverT _frameBufferResizedReceiver;
    GLFWwindow* _window;
    glm::uvec2 _size;
};

}

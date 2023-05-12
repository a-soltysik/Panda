#pragma once

#include <functional>
#include <glm/vec2.hpp>

namespace panda
{

class Window
{
public:
    Window(glm::uvec2 initialSize, const char* name);
    Window(const Window&) = delete;
    Window(Window&&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    auto operator=(Window&&) -> Window& = delete;
    ~Window() noexcept;

    [[nodiscard]] auto getHandle() const noexcept -> GLFWwindow*;
    [[nodiscard]] auto shouldClose() const noexcept -> bool;
    [[nodiscard]] auto isMinimized() const noexcept -> bool;
    [[nodiscard]] auto getSize() const noexcept -> glm::uvec2;
    static auto processInput() noexcept -> void;
    static auto waitForInput() noexcept -> void;
    auto subscribeForFrameBufferResize(std::function<void(int, int)>&& action) -> void;

private:
    friend auto framebufferResizeCallback(GLFWwindow* window, int width, int height) -> void;

    std::vector<std::function<void(int, int)>> frameBufferResizeSubscribers;
    GLFWwindow* window = nullptr;
};

}

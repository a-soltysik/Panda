#pragma once

#include <functional>
#include <glm/vec2.hpp>

#include "utils/Signal.h"

namespace panda
{

class Window
{
public:
    using FrameBufferResize = std::function<void(int, int)>;
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
    auto getFrameBufferResizeSignal() const noexcept -> utils::Signal<FrameBufferResize>&;

private:
    friend auto framebufferResizeCallback(GLFWwindow* window, int width, int height) -> void;

    const utils::Sender<FrameBufferResize> frameBufferResizeSender;
    GLFWwindow* window = nullptr;
};

}

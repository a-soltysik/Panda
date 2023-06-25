#pragma once

#include <functional>
#include <glm/vec2.hpp>

#include "utils/Signals.h"

namespace panda
{

class Window
{
public:
    Window(glm::uvec2 size, const char* name);
    PD_DELETE_ALL(Window);
    ~Window() noexcept;

    [[nodiscard]] auto getHandle() const noexcept -> GLFWwindow*;
    [[nodiscard]] auto shouldClose() const noexcept -> bool;
    [[nodiscard]] auto isMinimized() const noexcept -> bool;
    [[nodiscard]] auto getSize() const noexcept -> glm::uvec2;
    static auto processInput() noexcept -> void;
    static auto waitForInput() noexcept -> void;

private:
    [[nodiscard]] static auto createWindow(glm::uvec2 size, const char* name) -> GLFWwindow*;

    utils::Signals::FrameBufferResized::ReceiverT _frameBufferResizedReceiver {};
    GLFWwindow* _window;
    glm::uvec2 _size;
};

}

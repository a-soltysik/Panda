#pragma once

#include <GLFW/glfw3.h>
#include <panda/utils/Signals.h>

#include "utils/Signals.h"

namespace app
{

class GlfwWindow;

class MouseHandler
{
public:
    enum class ButtonState
    {
        JustReleased,
        Released,
        Pressed,
        JustPressed
    };

    explicit MouseHandler(const GlfwWindow& window);

    [[nodiscard]] auto getButtonState(int button) const -> ButtonState;
    [[nodiscard]] auto getCursorPosition() const -> glm::vec2;
    [[nodiscard]] auto getCursorDeltaPosition() const -> glm::vec2;

private:
    std::array<ButtonState, GLFW_MOUSE_BUTTON_LAST> _states {};
    glm::dvec2 _currentPosition {};
    glm::dvec2 _previousPosition {};

    utils::signals::MouseButtonStateChanged::ReceiverT _mouseButtonStateChangedReceiver;
    utils::signals::CursorPositionChanged::ReceiverT _cursorStateChangedReceiver;
    panda::utils::signals::GameLoopIterationStarted::ReceiverT _newFrameNotifReceiver;

    const GlfwWindow& _window;
};

}

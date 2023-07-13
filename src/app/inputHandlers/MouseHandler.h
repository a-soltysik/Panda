#pragma once

#include "app/Window.h"

namespace panda::app
{

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

    [[nodiscard]] static auto instance(const Window& window) -> const MouseHandler&;

    [[nodiscard]] auto getButtonState(int button) const -> ButtonState;
    [[nodiscard]] auto getCursorPosition() const -> glm::vec2;
    [[nodiscard]] auto getCursorDeltaPosition() const -> glm::vec2;

private:
    [[nodiscard]] static auto getInitialPosition(const Window& window) -> glm::dvec2;

    explicit MouseHandler(const Window& window);

    std::array<ButtonState, GLFW_MOUSE_BUTTON_LAST> _states {};
    glm::dvec2 _currentPosition{};
    glm::dvec2 _previousPosition{};

    utils::Signals::MouseButtonStateChanged::ReceiverT _mouseButtonStateChangedReceiver {};
    utils::Signals::CursorPositionChanged::ReceiverT _cursorStateChangedReceiver{};
    utils::Signals::GameLoopIterationStarted::ReceiverT _newFrameNotifReceiver {};

    const Window& _window;
};

}

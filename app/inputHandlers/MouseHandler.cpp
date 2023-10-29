#include "MouseHandler.h"

#include <imgui.h>

#include "GlfwWindow.h"
#include "utils/format/app/inputHandlers/MouseHandlerButtonStateFormatter.h"

namespace
{

void mouseButtonStateChangedCallback(GLFWwindow* window, int key, int action, int mods)
{
    static auto sender = app::utils::Signals::mouseButtonStateChanged.registerSender();
    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Debug("Mouse button state for window [{}] changed to {};{};{}", id, key, action, mods);
    if (ImGui::GetIO().WantCaptureMouse)
    {
        panda::log::Debug("ImGui has overtaken mouse input");
    }
    else
    {
        sender(id, key, action, mods);
    }
}

void cursorPositionChangedCallback(GLFWwindow* window, double x, double y)
{
    static auto sender = app::utils::Signals::cursorPositionChanged.registerSender();
    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Debug("Cursor position for window [{}] changed to ({}, {})", id, x, y);

    if (ImGui::GetIO().WantCaptureMouse)
    {
        panda::log::Debug("ImGui has overtaken mouse input");
    }
    else
    {
        sender(id, x, y);
    }
}

}

namespace app
{

MouseHandler::MouseHandler(const GlfwWindow& window)
    : _currentPosition {},
      _previousPosition {_currentPosition},
      _window {window}
{
    _states.fill(ButtonState::Released);

    _window.setMouseButtonCallback(mouseButtonStateChangedCallback);
    _window.setCursorPositionCallback(cursorPositionChangedCallback);

    _mouseButtonStateChangedReceiver = utils::Signals::mouseButtonStateChanged.connect(
        [this](GlfwWindow::Id id, int button, int action, [[maybe_unused]] int mods) {
            if (id != _window.getId())
            {
                return;
            }
            const auto isCorrectButton = button < static_cast<int>(_states.size()) && button >= 0;
            if (!panda::shouldBe(isCorrectButton, fmt::format("Button: {} is beyond the size of array", button)))
            {
                return;
            }

            if ((_states[static_cast<size_t>(button)] == ButtonState::JustReleased ||
                 _states[static_cast<size_t>(button)] == ButtonState::Released) &&
                action == GLFW_PRESS)
            {
                _states[static_cast<size_t>(button)] = ButtonState::JustPressed;
            }
            else if ((_states[static_cast<size_t>(button)] == ButtonState::JustPressed ||
                      _states[static_cast<size_t>(button)] == ButtonState::Pressed) &&
                     action == GLFW_RELEASE)
            {
                _states[static_cast<size_t>(button)] = ButtonState::JustReleased;
            }
        });

    _cursorStateChangedReceiver =
        utils::Signals::cursorPositionChanged.connect([this](GlfwWindow::Id id, double x, double y) {
            if (id != _window.getId())
            {
                return;
            }
            _previousPosition = _currentPosition;
            _currentPosition = {x, y};
        });

    _newFrameNotifReceiver = panda::utils::Signals::gameLoopIterationStarted.connect([this]() {
        _previousPosition = _currentPosition;
        for (auto& state : _states)
        {
            if (state == ButtonState::JustReleased)
            {
                state = ButtonState::Released;
            }
            if (state == ButtonState::JustPressed)
            {
                state = ButtonState::Pressed;
            }
        }
    });
}

auto MouseHandler::getButtonState(int button) const -> ButtonState
{
    const auto isCorrectButton = button < static_cast<int>(_states.size()) && button >= 0;

    panda::expect(isCorrectButton, fmt::format("Button: {} is beyond the size of array", button));

    panda::log::Debug("State of button \"{}\": {}", button, _states[static_cast<size_t>(button)]);

    return _states[static_cast<size_t>(button)];
}

auto MouseHandler::getCursorPosition() const -> glm::vec2
{
    return glm::vec2 {_currentPosition};
}

auto MouseHandler::getCursorDeltaPosition() const -> glm::vec2
{
    return glm::vec2 {_currentPosition - _previousPosition};
}
}

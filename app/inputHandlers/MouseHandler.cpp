#include "MouseHandler.h"

#include <imgui.h>

#include "GlfwWindow.h"
#include "utils/format/app/inputHandlers/MouseHandlerButtonStateFormatter.h"

namespace
{

using namespace app::utils::signals;

void mouseButtonStateChangedCallback(GLFWwindow* window, int key, int action, int mods)
{
    static auto sender = app::utils::signals::mouseButtonStateChanged.registerSender();
    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Debug("Mouse button state for window [{}] changed to {};{};{}", id, key, action, mods);
    if (!ImGui::GetIO().WantCaptureMouse)
    {
        sender(
            app::utils::signals::MouseButtonStateChangedData {.id = id, .button = key, .action = action, .mods = mods});
    }
}

void cursorPositionChangedCallback(GLFWwindow* window, double x, double y)
{
    static auto sender = app::utils::signals::cursorPositionChanged.registerSender();
    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Debug("Cursor position for window [{}] changed to ({}, {})", id, x, y);

    if (!ImGui::GetIO().WantCaptureMouse)
    {
        sender(app::utils::signals::CursorPositionChangedData {.id = id, .x = x, .y = y});
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

    _mouseButtonStateChangedReceiver = utils::signals::mouseButtonStateChanged.connect([this](auto data) {
        if (data.id != _window.getId())
        {
            return;
        }
        const auto isCorrectButton = data.button < static_cast<int>(_states.size()) && data.button >= 0;
        if (!panda::shouldBe(isCorrectButton, fmt::format("Button: {} is beyond the size of array", data.button)))
        {
            return;
        }

        if ((_states[static_cast<size_t>(data.button)] == ButtonState::JustReleased ||
             _states[static_cast<size_t>(data.button)] == ButtonState::Released) &&
            data.action == GLFW_PRESS)
        {
            _states[static_cast<size_t>(data.button)] = ButtonState::JustPressed;
        }
        else if ((_states[static_cast<size_t>(data.button)] == ButtonState::JustPressed ||
                  _states[static_cast<size_t>(data.button)] == ButtonState::Pressed) &&
                 data.action == GLFW_RELEASE)
        {
            _states[static_cast<size_t>(data.button)] = ButtonState::JustReleased;
        }
    });

    _cursorStateChangedReceiver = utils::signals::cursorPositionChanged.connect([this](auto data) {
        if (data.id != _window.getId())
        {
            return;
        }
        _previousPosition = _currentPosition;
        _currentPosition = {data.x, data.y};
    });

    _newFrameNotifReceiver = panda::utils::signals::gameLoopIterationStarted.connect([this]() {
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
    const auto isCorrectButton = static_cast<size_t>(button) < _states.size() && button >= 0;

    panda::expect(isCorrectButton, fmt::format("Button: {} is beyond the size of array", button));

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

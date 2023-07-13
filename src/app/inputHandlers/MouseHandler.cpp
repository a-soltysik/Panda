#include "MouseHandler.h"
#include "utils/format/app/inputHandlers/MouseHandlerButtonStateFormatter.h"

namespace
{

void mouseButtonStateChangedCallback(GLFWwindow* window, int key, int action, int mods)
{
    static auto sender = panda::utils::Signals::mouseButtonStateChanged.registerSender();
    panda::log::Debug("Mouse button state for window [{}] changed to {};{};{}",
                      static_cast<void*>(window),
                      key,
                      action,
                      mods);

    sender(key, action, mods);
}

void cursorPositionChangedCallback(GLFWwindow* window, double x, double y)
{
    static auto sender = panda::utils::Signals::cursorPositionChanged.registerSender();
    panda::log::Debug("Cursor position for window [{}] changed to ({}, {})",
                      static_cast<void*>(window),
                      x, y);

    sender(x, y);
}

}

namespace panda::app
{

MouseHandler::MouseHandler(const Window& window)
    : _currentPosition{getInitialPosition(window)},
      _previousPosition{_currentPosition},
      _window{window}
{
    _states.fill(ButtonState::Released);

    glfwSetMouseButtonCallback(_window.getHandle(), mouseButtonStateChangedCallback);
    glfwSetCursorPosCallback(_window.getHandle(), cursorPositionChangedCallback);

    _mouseButtonStateChangedReceiver =
        utils::Signals::mouseButtonStateChanged.connect([this](int button, int action, [[maybe_unused]] int mods) {
            const auto isCorrectButton = button < static_cast<int>(_states.size()) && button >= 0;
            if (!shouldBe(isCorrectButton, fmt::format("Button: {} is beyond the size of array", button)))
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

    _cursorStateChangedReceiver = utils::Signals::cursorPositionChanged.connect([this](double x, double y) {
        _previousPosition = _currentPosition;
        _currentPosition = {x, y};
    });

    _newFrameNotifReceiver = utils::Signals::gameLoopIterationStarted.connect([this]() {
        _previousPosition = _currentPosition;
        for (auto i = uint32_t {}; i < _states.size(); i++)
        {
            if (_states[i] == ButtonState::JustReleased)
            {
                _states[i] = ButtonState::Released;
            }
            if (_states[i] == ButtonState::JustPressed)
            {
                _states[i] = ButtonState::Pressed;
            }
        }
    });
}

auto MouseHandler::instance(const Window& window) -> const MouseHandler&
{
    static const auto mouseHandler = MouseHandler {window};

    expect(mouseHandler._window.getHandle(),
           window.getHandle(),
           fmt::format("Can't register for window [{}]. KeyboardHandler is already registered for window [{}]",
                       static_cast<void*>(window.getHandle()),
                       static_cast<void*>(mouseHandler._window.getHandle())));

    return mouseHandler;
}

auto MouseHandler::getButtonState(int button) const -> ButtonState
{
    const auto isCorrectButton = button < static_cast<int>(_states.size()) && button >= 0;

    expect(
        isCorrectButton,
        fmt::format("Button: {} is beyond the size of array", button));

    log::Debug("State of button \"{}\": {}", button, _states[static_cast<size_t>(button)]);

    return _states[static_cast<size_t>(button)];
}

auto MouseHandler::getCursorPosition() const -> glm::vec2
{
    return glm::vec2{_currentPosition};
}

auto MouseHandler::getCursorDeltaPosition() const -> glm::vec2
{
    return glm::vec2{_currentPosition - _previousPosition};
}

auto MouseHandler::getInitialPosition(const Window& window) -> glm::dvec2
{
    auto position = glm::dvec2 {};
    glfwGetCursorPos(window.getHandle(), &position.x, &position.y);

    return position;
}

}

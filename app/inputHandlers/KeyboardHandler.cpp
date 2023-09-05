#include "KeyboardHandler.h"

#include "GlfwWindow.h"
#include "utils/Signals.h"
#include "utils/format/app/inputHandlers/KeyboardHandlerStateFormatter.h"

namespace
{

void keyboardStateChangedCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    static auto sender = app::utils::Signals::keyboardStateChanged.registerSender();
    panda::log::Debug("Keyboard state for window [{}] changed to {};{};{};{}",
                      static_cast<void*>(window),
                      key,
                      scancode,
                      action,
                      mods);

    sender(app::GlfwWindow::makeId(window), key, scancode, action, mods);
}

}

namespace app
{

KeyboardHandler::KeyboardHandler(const GlfwWindow& window)
    : _window {window}
{
    _states.fill(State::Released);

    [[maybe_unused]] static const auto oldKeyCallback = _window.setKeyCallback(keyboardStateChangedCallback);

    _keyboardStateChangedReceiver = utils::Signals::keyboardStateChanged.connect(
        [this](GlfwWindow::Id id, int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods) {
            if (id != _window.getId())
            {
                return;
            }

            const auto isCorrectKey = key < static_cast<int>(_states.size()) && key >= 0;
            if (!panda::shouldBe(isCorrectKey, fmt::format("Key: {} is beyond the size of array", key)))
            {
                return;
            }

            if ((_states[static_cast<size_t>(key)] == State::JustReleased ||
                 _states[static_cast<size_t>(key)] == State::Released) &&
                (action == GLFW_PRESS || action == GLFW_REPEAT))
            {
                _states[static_cast<size_t>(key)] = State::JustPressed;
            }
            else if ((_states[static_cast<size_t>(key)] == State::JustPressed ||
                      _states[static_cast<size_t>(key)] == State::Pressed) &&
                     action == GLFW_RELEASE)
            {
                _states[static_cast<size_t>(key)] = State::JustReleased;
            }
        });

    _newFrameNotifReceiver = panda::utils::Signals::gameLoopIterationStarted.connect([this]() {
        for (auto i = uint32_t {}; i < _states.size(); i++)
        {
            if (_states[i] == State::JustReleased)
            {
                _states[i] = State::Released;
            }
            if (_states[i] == State::JustPressed)
            {
                _states[i] = State::Pressed;
            }
        }
    });
}

auto KeyboardHandler::getKeyState(int key) const -> State
{
    panda::expect(
        key,
        [this](auto userKey) {
            return userKey < static_cast<int>(_states.size()) && userKey >= 0;
        },
        fmt::format("Key: {} is beyond the size of array", key));

    panda::log::Debug("State of key \"{}\": {}", key, _states[static_cast<size_t>(key)]);

    return _states[static_cast<size_t>(key)];
}

}
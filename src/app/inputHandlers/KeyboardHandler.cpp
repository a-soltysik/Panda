#include "KeyboardHandler.h"

#include "utils/Signals.h"
#include "utils/format/app/inputHandlers/KeyboardHandlerStateFormatter.h"

namespace
{

void keyboardStateChangedCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    static auto sender = panda::utils::Signals::keyboardStateChanged.registerSender();
    panda::log::Debug("Keyboard state for window [{}] changed to {};{};{};{}",
                      static_cast<void*>(window),
                      key,
                      scancode,
                      action,
                      mods);

    sender(key, scancode, action, mods);
}

}

namespace panda::app
{

KeyboardHandler::KeyboardHandler(const Window& window)
    : _window {window}
{
    _states.fill(State::Released);

    glfwSetKeyCallback(_window.getHandle(), keyboardStateChangedCallback);

    _keyboardStateChangedReceiver = utils::Signals::keyboardStateChanged.connect(
        [this](int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods) {
            const auto isCorrectKey = key < static_cast<int>(_states.size()) && key >= 0;
            if (!shouldBe(isCorrectKey, fmt::format("Key: {} is beyond the size of array", key)))
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

    _newFrameNotifReceiver = utils::Signals::gameLoopIterationStarted.connect([this]() {
        for (auto i = uint32_t{}; i < _states.size(); i++)
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
    expect(
        key,
        [this](auto userKey) {
            return userKey < static_cast<int>(_states.size()) && userKey >= 0;
        },
        fmt::format("Key: {} is beyond the size of array", key));

    log::Debug("State of key \"{}\": {}", key, _states[static_cast<size_t>(key)]);

    return _states[static_cast<size_t>(key)];
}

auto KeyboardHandler::instance(const Window& window) -> const KeyboardHandler&
{
    static const auto keyboardHandler = KeyboardHandler {window};

    expect(keyboardHandler._window.getHandle(),
           window.getHandle(),
           fmt::format("Can't register for window [{}]. KeyboardHandler is already registered for window [{}]",
                       static_cast<void*>(window.getHandle()),
                       static_cast<void*>(keyboardHandler._window.getHandle())));

    return keyboardHandler;
}

}
#include "KeyboardHandler.h"

#include <imgui.h>

#include "GlfwWindow.h"
#include "utils/Signals.h"
#include "utils/format/app/inputHandlers/KeyboardHandlerStateFormatter.h"

namespace
{

void keyboardStateChangedCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    static auto sender = app::utils::signals::keyboardStateChanged.registerSender();

    const auto id = app::GlfwWindow::makeId(window);
    panda::log::Debug("Keyboard state for window [{}] changed to {};{};{};{}", id, key, scancode, action, mods);

    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        sender(app::utils::signals::KeyboardStateChangedData {id, key, scancode, action, mods});
    }
}

}

namespace app
{

KeyboardHandler::KeyboardHandler(const GlfwWindow& window)
    : _window {window}
{
    _states.fill(State::Released);

    [[maybe_unused]] static const auto oldKeyCallback = _window.setKeyCallback(keyboardStateChangedCallback);

    _keyboardStateChangedReceiver = utils::signals::keyboardStateChanged.connect([this](auto data) {
        if (data.id != _window.getId())
        {
            return;
        }

        const auto isCorrectKey = data.key < static_cast<int>(_states.size()) && data.key >= 0;
        if (!panda::shouldBe(isCorrectKey, fmt::format("Key: {} is beyond the size of array", data.key)))
        {
            return;
        }

        if ((_states[static_cast<size_t>(data.key)] == State::JustReleased ||
             _states[static_cast<size_t>(data.key)] == State::Released) &&
            (data.action == GLFW_PRESS || data.action == GLFW_REPEAT))
        {
            _states[static_cast<size_t>(data.key)] = State::JustPressed;
        }
        else if ((_states[static_cast<size_t>(data.key)] == State::JustPressed ||
                  _states[static_cast<size_t>(data.key)] == State::Pressed) &&
                 data.action == GLFW_RELEASE)
        {
            _states[static_cast<size_t>(data.key)] = State::JustReleased;
        }
    });

    _newFrameNotifReceiver = panda::utils::signals::gameLoopIterationStarted.connect([this]() {
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

    return _states[static_cast<size_t>(key)];
}

}
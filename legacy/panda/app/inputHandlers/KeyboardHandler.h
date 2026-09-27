#pragma once

#include <GLFW/glfw3.h>
#include <panda/utils/Signal.h>
#include <panda/utils/Signals.h>

#include <array>
#include <cstdint>

#include "utils/Signals.h"

namespace app
{

class GlfwWindow;

class KeyboardHandler
{
public:
    enum class State : uint8_t
    {
        JustReleased,
        Released,
        Pressed,
        JustPressed
    };

    explicit KeyboardHandler(const GlfwWindow& window);

    [[nodiscard]] auto getKeyState(int key) const -> State;

private:
    std::array<State, GLFW_KEY_LAST> _states {};
    utils::signals::KeyboardStateChanged::Signal::ReceiverT _keyboardStateChangedReceiver;
    panda::utils::signals::GameLoopIterationStarted::ReceiverT _newFrameNotifReceiver;
    const GlfwWindow& _window;
};

}

#pragma once

#include <array>

#include "app/Window.h"
#include "utils/Signals.h"

namespace panda::app
{

class KeyboardHandler
{
public:
    enum class State
    {
        JustReleased,
        Released,
        Pressed,
        JustPressed
    };

    [[nodiscard]] static auto instance(const Window& window) -> const KeyboardHandler&;

    [[nodiscard]] auto getKeyState(int key) const -> State;

private:
    explicit KeyboardHandler(const Window& window);

    std::array<State, GLFW_KEY_LAST> _states {};
    utils::Signals::KeyboardStateChanged::ReceiverT _keyboardStateChangedReceiver {};
    utils::Signals::GameLoopIterationStarted::ReceiverT  _newFrameNotifReceiver{};
    const Window& _window;
};

}

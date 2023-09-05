#pragma once

#include <panda/Window.h>
#include <panda/utils/Signal.h>

namespace app::utils
{

struct Signals
{
    using KeyboardStateChanged = panda::utils::Signal<panda::Window::Id, int, int, int, int>;
    inline static auto keyboardStateChanged = KeyboardStateChanged {};

    using MouseButtonStateChanged = panda::utils::Signal<panda::Window::Id, int, int, int>;
    inline static auto mouseButtonStateChanged = MouseButtonStateChanged {};

    using CursorPositionChanged = panda::utils::Signal<panda::Window::Id, double, double>;
    inline static auto cursorPositionChanged = CursorPositionChanged {};
};

}
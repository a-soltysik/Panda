#pragma once

#include <functional>

#include "Signal.h"

namespace panda::utils
{

struct Signals
{
    using FrameBufferResized = Signal<int, int>;
    inline static auto frameBufferResized = FrameBufferResized {};

    using KeyboardStateChanged = Signal<int, int, int, int>;
    inline static auto keyboardStateChanged = KeyboardStateChanged {};

    using MouseButtonStateChanged = Signal<int, int, int>;
    inline static auto mouseButtonStateChanged = MouseButtonStateChanged {};

    using CursorPositionChanged = Signal<double, double>;
    inline static auto cursorPositionChanged = CursorPositionChanged {};

    using GameLoopIterationStarted = Signal<>;
    inline static auto gameLoopIterationStarted = GameLoopIterationStarted {};
};

}
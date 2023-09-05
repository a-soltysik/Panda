#pragma once

#include "Signal.h"

namespace panda::utils
{

struct Signals
{
    using FrameBufferResized = Signal<int, int>;
    inline static auto frameBufferResized = FrameBufferResized {};

    using GameLoopIterationStarted = Signal<>;
    inline static auto gameLoopIterationStarted = GameLoopIterationStarted {};
};

}
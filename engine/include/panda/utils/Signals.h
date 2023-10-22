#pragma once

#include "Signal.h"
#include "panda/Common.h"

namespace panda::utils
{

struct Signals
{
    using FrameBufferResized = Signal<int, int>;
    inline static auto frameBufferResized = FrameBufferResized {};

    using GameLoopIterationStarted = Signal<>;
    inline static auto gameLoopIterationStarted = GameLoopIterationStarted {};

    using BeginGuiRender = Signal<vk::CommandBuffer>;
    inline static auto beginGuiRender = BeginGuiRender {};
};

}
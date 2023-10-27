#pragma once

#include "Signal.h"
#include "panda/Common.h"
#include "panda/gfx/vulkan/Scene.h"

namespace panda::utils
{

struct Signals
{
    using FrameBufferResized = Signal<int, int>;
    inline static auto frameBufferResized = FrameBufferResized {};

    using GameLoopIterationStarted = Signal<>;
    inline static auto gameLoopIterationStarted = GameLoopIterationStarted {};

    using BeginGuiRender = Signal<vk::CommandBuffer, gfx::vulkan::Scene&>;
    inline static auto beginGuiRender = BeginGuiRender {};
};

}
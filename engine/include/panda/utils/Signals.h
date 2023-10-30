#pragma once

#include "Signal.h"
#include "panda/Common.h"
#include "panda/Window.h"
#include "panda/gfx/vulkan/Scene.h"

namespace panda::utils::signals
{
struct FrameBufferResizedData
{
    Window::Id id;
    int x;
    int y;
};

using FrameBufferResized = Signal<FrameBufferResizedData>;

struct BeginGuiRenderData
{
    vk::CommandBuffer commandBuffer;
    std::reference_wrapper<gfx::vulkan::Scene> scene;
};

using BeginGuiRender = Signal<BeginGuiRenderData>;

using GameLoopIterationStarted = Signal<>;

inline auto frameBufferResized = FrameBufferResized {};
inline auto beginGuiRender = BeginGuiRender {};
inline auto gameLoopIterationStarted = GameLoopIterationStarted {};

}
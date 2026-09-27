#pragma once

#include <drip/common/utils/Signal.hpp>

#include "drip/gfx/Window.hpp"

namespace drip::gfx::signal
{
struct FrameBufferResizedData
{
    Window::Id id;
    int x;
    int y;
};

using FrameBufferResized = common::signal::Signal<FrameBufferResizedData>;
inline auto frameBufferResized = FrameBufferResized {};

}

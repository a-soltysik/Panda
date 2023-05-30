#pragma once

#include <functional>

#include "Signal.h"

namespace panda::utils
{

struct Signals
{
    using FrameBufferResized = Signal<int, int>;
    inline static auto frameBufferResized = FrameBufferResized {};
};

}
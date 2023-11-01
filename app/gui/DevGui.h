#pragma once

#include <imgui.h>
#include <panda/utils/Signals.h>

#include "backend/Profiler.h"

namespace app
{

class DevGui
{
public:
    explicit DevGui(const panda::Window& window);
    auto render() -> void;

private:
    using MemorySize =
        unsigned long long;  //Workaround for defining size_t as unsigned long, since ImPLot has no definitions for such type
    std::array<MemorySize, 60> _physicalMemoryUsages {};
    std::array<float, 60> _frameRates {};
    std::array<float, 60> _cpuUsages {};

    float _time = 0.f;

    const panda::Window& _window;
};

}

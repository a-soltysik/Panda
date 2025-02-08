#pragma once

#include <panda/Logger.h>

#include <array>
#include <cstdint>
#include <deque>

namespace panda
{
class Window;
}

namespace app
{

class DevGui
{
public:
    explicit DevGui(const panda::Window& window);
    auto render() -> void;

private:
    auto renderLogger() -> void;
    auto renderProfiler() -> void;
    auto updateProfiler() -> void;
    auto updateLogs() -> void;

    //Workaround for defining size_t as unsigned long, since ImPlot has no definitions for such type
    using MemorySize = unsigned long long;
    std::array<MemorySize, 60> _physicalMemoryUsages {};
    std::array<MemorySize, 60> _virtualMemoryUsages {};
    std::array<float, 60> _frameRates {};
    std::array<float, 60> _cpuUsages {};
    std::array<bool, 2> _openedTabs {true, true};
    std::array<const char*, 2> _tabsNames {"Profiler", "Logger"};
    std::deque<panda::log::FileLogger::LogData> _logs;
    uint32_t _lastMaxIndex = 0;
    float _time = 0.F;
    bool _anchorLogs = true;

    const panda::Window& _window;
};

}

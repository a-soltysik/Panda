#pragma once

#include <sys/sysinfo.h>

#include <ctime>

namespace app::backend::linux
{

class Profiler
{
public:
    Profiler();
    [[nodiscard]] auto getCurrentUsageInPercents() -> double;

    [[nodiscard]] auto getTotalVirtualMemory() const -> size_t;  // cppcheck-suppress functionStatic
    [[nodiscard]] auto getVirtualMemoryUsage() const -> size_t;

    [[nodiscard]] auto getTotalPhysicalMemory() const -> size_t;  // cppcheck-suppress functionStatic
    [[nodiscard]] auto getPhysicalMemoryUsage() const -> size_t;

private:
    struct TimeInfo
    {
        clock_t idle;
        clock_t system;
        clock_t user;
    };

    [[nodiscard]] static auto getCurrentTimeInfo() -> TimeInfo;
    [[nodiscard]] static auto getMemoryInfo() -> struct sysinfo;

    TimeInfo _lastTimeInfo;
    size_t _threadsCount;
    size_t _pageSize;
};

}
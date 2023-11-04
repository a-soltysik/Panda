#pragma once

// clang-format off
#include <windows.h>
#include <psapi.h>

// clang-format on

#undef near
#undef far

namespace app::backend::windows
{

class Profiler
{
public:
    Profiler();
    auto getCurrentUsageInPercents() -> double;
    [[nodiscard]] auto getTotalVirtualMemory() const -> size_t;
    [[nodiscard]] auto getVirtualMemoryUsage() -> size_t;

    [[nodiscard]] auto getTotalPhysicalMemory() const -> size_t;
    [[nodiscard]] auto getPhysicalMemoryUsage() -> size_t;

private:
    struct TimeInfo
    {
        ULARGE_INTEGER idle;
        ULARGE_INTEGER system;
        ULARGE_INTEGER user;
    };

    [[nodiscard]] static auto getMemoryStatus() -> MEMORYSTATUSEX;
    [[nodiscard]] auto getProcessMemory() const -> PROCESS_MEMORY_COUNTERS_EX;
    [[nodiscard]] static auto getCurrentTime() -> ULARGE_INTEGER;
    [[nodiscard]] static auto getThreadsCount() -> DWORD;
    [[nodiscard]] auto getCurrentTimeInfo() -> TimeInfo;

    MEMORYSTATUSEX _memoryStatus;
    TimeInfo _lastTimeInfo {};
    DWORD _threadsCount;
    HANDLE _process;
};

}
#pragma once

// clang-format off
#include <windows.h>
#include <psapi.h>

// clang-format on

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
    [[nodiscard]] static auto getMemoryStatus() -> MEMORYSTATUSEX;
    [[nodiscard]] auto getProcessMemory() const -> PROCESS_MEMORY_COUNTERS_EX;
    [[nodiscard]] static auto getCurrentTime() -> ULARGE_INTEGER;
    [[nodiscard]] static auto getThreadsCount() -> DWORD;

    MEMORYSTATUSEX _memoryStatus {};
    ULARGE_INTEGER _lastTime {};
    ULARGE_INTEGER _lastSystemTime {};
    ULARGE_INTEGER _lastUserTime {};
    DWORD _threadsCount {};
    HANDLE _process;
};

}
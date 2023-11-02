#include "Profiler.h"

#include <bit>

#include "Common.h"

namespace app::backend::windows
{

Profiler::Profiler()
    : _memoryStatus {getMemoryStatus()},
      _lastTimeInfo {getCurrentTimeInfo()},
      _threadsCount {getThreadsCount()},
      _process {GetCurrentProcess()}
{
    panda::log::Info("Number of available threads: {}", _threadsCount);
}

auto Profiler::getCurrentUsageInPercents() -> double
{
    const auto currentTimeInfo = getCurrentTimeInfo();
    auto percent = static_cast<double>((currentTimeInfo.system.QuadPart - _lastTimeInfo.system.QuadPart) +
                                       (currentTimeInfo.user.QuadPart - _lastTimeInfo.user.QuadPart));
    percent /= static_cast<double>(currentTimeInfo.idle.QuadPart - _lastTimeInfo.idle.QuadPart);
    percent /= static_cast<double>(_threadsCount);

    _lastTimeInfo = currentTimeInfo;

    return percent * 100.;
}

auto Profiler::getTotalVirtualMemory() const -> size_t
{
    return _memoryStatus.ullTotalPageFile;
}

auto Profiler::getVirtualMemoryUsage() -> size_t
{
    return getProcessMemory().PrivateUsage;
}

auto Profiler::getTotalPhysicalMemory() const -> size_t
{
    return _memoryStatus.ullTotalPhys;
}

auto Profiler::getPhysicalMemoryUsage() -> size_t
{
    return getProcessMemory().WorkingSetSize;
}

auto Profiler::getMemoryStatus() -> MEMORYSTATUSEX
{
    auto memoryInfo = MEMORYSTATUSEX {};
    memoryInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memoryInfo);

    return memoryInfo;
}

auto Profiler::getProcessMemory() const -> PROCESS_MEMORY_COUNTERS_EX
{
    auto pmc = PROCESS_MEMORY_COUNTERS_EX {};
    GetProcessMemoryInfo(_process, reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&pmc), sizeof(pmc));

    return pmc;
}

auto Profiler::getCurrentTime() -> ULARGE_INTEGER
{
    auto currentTime = FILETIME {};
    GetSystemTimeAsFileTime(&currentTime);
    return std::bit_cast<ULARGE_INTEGER>(currentTime);
}

auto Profiler::getThreadsCount() -> DWORD
{
    auto systemInfo = SYSTEM_INFO {};
    GetSystemInfo(&systemInfo);
    return systemInfo.dwNumberOfProcessors;
}

auto Profiler::getCurrentTimeInfo() -> Profiler::TimeInfo
{
    auto system = FILETIME {};
    auto user = FILETIME {};
    auto dummy = FILETIME {};
    GetProcessTimes(_process, &dummy, &dummy, &system, &user);

    return {.idle = getCurrentTime(),
            .system = std::bit_cast<ULARGE_INTEGER>(system),
            .user = std::bit_cast<ULARGE_INTEGER>(user)};
}

}
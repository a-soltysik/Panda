#include "Profiler.h"

#include <panda/Logger.h>
#include <sys/times.h>
#include <unistd.h>

#include <fstream>

namespace
{

constexpr auto memoryStatusPath = "/proc/self/statm";

}

namespace app::backend::linux
{

Profiler::Profiler()
    : _lastTimeInfo {getCurrentTimeInfo()},
      _threadsCount {static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN))},
      _pageSize {static_cast<size_t>(sysconf(_SC_PAGESIZE))}
{
    panda::log::Info("Memory page size: {}", _pageSize);
    panda::log::Info("Number of available threads: {}", _threadsCount);
}

auto Profiler::getCurrentUsageInPercents() -> double
{
    const auto currentTimeInfo = getCurrentTimeInfo();
    auto percent = static_cast<double>((currentTimeInfo.system - _lastTimeInfo.system) +
                                       (currentTimeInfo.user - _lastTimeInfo.user));

    percent /= static_cast<double>(currentTimeInfo.idle - _lastTimeInfo.idle);
    percent /= static_cast<double>(_threadsCount);

    _lastTimeInfo = currentTimeInfo;

    return percent * 100.;
}

auto Profiler::getTotalVirtualMemory() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    const auto memoryInfo = getMemoryInfo();
    const auto totalVirtualMem = size_t {memoryInfo.totalram} + size_t {memoryInfo.totalswap};
    return totalVirtualMem * memoryInfo.mem_unit;
}

auto Profiler::getVirtualMemoryUsage() const -> size_t
{
    auto fin = std::ifstream {memoryStatusPath};
    auto virtualMemoryUsage = size_t {};

    fin >> virtualMemoryUsage;
    return virtualMemoryUsage * _pageSize;
}

auto Profiler::getTotalPhysicalMemory() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    const auto memoryInfo = getMemoryInfo();
    return size_t {memoryInfo.totalram} * memoryInfo.mem_unit;
}

auto Profiler::getPhysicalMemoryUsage() const -> size_t
{
    auto fin = std::ifstream {memoryStatusPath};
    auto virtualMemoryUsage = size_t {};
    auto physicalMemoryUsage = size_t {};

    fin >> virtualMemoryUsage >> physicalMemoryUsage;

    return physicalMemoryUsage * _pageSize;
}

auto Profiler::getCurrentTimeInfo() -> Profiler::TimeInfo
{
    auto timeSample = tms {};

    const auto currentTime = times(&timeSample);

    return {.idle = currentTime, .system = timeSample.tms_stime, .user = timeSample.tms_utime};
}

auto Profiler::getMemoryInfo() -> struct sysinfo
{
    struct sysinfo memoryInfo
    {
    };

    sysinfo(&memoryInfo);

    return memoryInfo;
}

}
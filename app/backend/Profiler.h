#pragma once

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#    include "windows/Profiler.h"
#elif defined(__linux__)
#    include "linux/Profiler.h"
#else
#    include "other/Profiler.h"
#endif

#include <concepts>

namespace app::backend
{

template <typename T>
concept ProfilerImpl = requires(T profiler) {
    { profiler.getCurrentUsageInPercents() } -> std::same_as<double>;

    { profiler.getTotalVirtualMemory() } -> std::same_as<size_t>;
    { profiler.getVirtualMemoryUsage() } -> std::same_as<size_t>;

    { profiler.getTotalPhysicalMemory() } -> std::same_as<size_t>;
    { profiler.getPhysicalMemoryUsage() } -> std::same_as<size_t>;
};

template <ProfilerImpl T>
class Profiler
{
public:
    auto getCpuUsageInPercents() -> double
    {
        return profiler.getCurrentUsageInPercents();
    }

    auto getTotalVirtualMemory() -> size_t
    {
        return profiler.getTotalVirtualMemory();
    }

    auto getVirtualMemoryUsage() -> size_t
    {
        return profiler.getVirtualMemoryUsage();
    }

    auto getTotalPhysicalMemory() -> size_t
    {
        return profiler.getTotalPhysicalMemory();
    }

    auto getPhysicalMemoryUsage() -> size_t
    {
        return profiler.getPhysicalMemoryUsage();
    }

private:
    T profiler;
};

inline auto getProfiler() -> decltype(auto)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    static auto profiler = Profiler<windows::Profiler> {};
    return profiler;
#elif defined(__linux__)
    static auto profiler = Profiler<linux::Profiler> {};
    return profiler;
#else
    static auto profiler = Profiler<other::Profiler> {};
    return profiler;
#endif
}
}
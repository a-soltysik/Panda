#pragma once

#include <cstddef>

namespace app::backend::unix
{

class Profiler
{
public:
    auto getCurrentUsageInPercents() -> double;  // cppcheck-suppress functionStatic

    auto getTotalVirtualMemory() -> size_t;  // cppcheck-suppress functionStatic
    auto getVirtualMemoryUsage() -> size_t;  // cppcheck-suppress functionStatic

    auto getTotalPhysicalMemory() -> size_t;  // cppcheck-suppress functionStatic
    auto getPhysicalMemoryUsage() -> size_t;  // cppcheck-suppress functionStatic
};

}
#include "Profiler.h"

namespace app::backend::unix
{

//NOLINTBEGIN(functionStatic, readability-convert-member-functions-to-static)

auto Profiler::getCurrentUsageInPercents() -> double
{
    return 0.;
}

auto Profiler::getTotalVirtualMemory() -> size_t
{
    return 0;
}

auto Profiler::getVirtualMemoryUsage() -> size_t
{
    return 0;
}

auto Profiler::getTotalPhysicalMemory() -> size_t
{
    return 0;
}

auto Profiler::getPhysicalMemoryUsage() -> size_t
{
    return 0;
}

//NOLINTEND(functionStatic, readability-convert-member-functions-to-static)
}
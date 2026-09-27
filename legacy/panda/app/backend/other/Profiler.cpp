#include "Profiler.h"

namespace app::backend::other
{
auto Profiler::getCurrentUsageInPercents() -> double  //NOLINT(readability-convert-member-functions-to-static)
{
    return 0.;
}

auto Profiler::getTotalVirtualMemory() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    return 0;
}

auto Profiler::getVirtualMemoryUsage() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    return 0;
}

auto Profiler::getTotalPhysicalMemory() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    return 0;
}

auto Profiler::getPhysicalMemoryUsage() const -> size_t  //NOLINT(readability-convert-member-functions-to-static)
{
    return 0;
}
}

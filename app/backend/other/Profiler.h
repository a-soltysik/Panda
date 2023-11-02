#pragma once

#include <cstddef>

namespace app::backend::other
{

class Profiler
{
public:
    [[nodiscard]] auto getCurrentUsageInPercents() -> double;  // cppcheck-suppress functionStatic

    [[nodiscard]] auto getTotalVirtualMemory() const -> size_t;  // cppcheck-suppress functionStatic
    [[nodiscard]] auto getVirtualMemoryUsage() const -> size_t;  // cppcheck-suppress functionStatic

    [[nodiscard]] auto getTotalPhysicalMemory() const -> size_t;  // cppcheck-suppress functionStatic
    [[nodiscard]] auto getPhysicalMemoryUsage() const -> size_t;  // cppcheck-suppress functionStatic
};

}
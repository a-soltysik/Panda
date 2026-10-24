#pragma once

#include <cstdint>
#include <tuple>

#ifdef _WIN32
#    include <windows.h>
#elifdef __linux__
#    include <unistd.h>
#endif

namespace panda::core::vulkan::external
{
class ScopedExternalHandle final
{
public:
    explicit ScopedExternalHandle(std::intptr_t value) noexcept
        : _value {value}
    {
    }

    ScopedExternalHandle(const ScopedExternalHandle&) = delete;
    auto operator=(const ScopedExternalHandle&) -> ScopedExternalHandle& = delete;
    ScopedExternalHandle(ScopedExternalHandle&&) = delete;
    auto operator=(ScopedExternalHandle&&) -> ScopedExternalHandle& = delete;

    ~ScopedExternalHandle()
    {
#ifdef _WIN32
        if (_value != 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): HANDLE is stored as intptr_t by the generic external memory API.
            CloseHandle(reinterpret_cast<HANDLE>(_value));
        }
#elifdef __linux__
        if (_value >= 0)
        {
            std::ignore = close(static_cast<int>(_value));
        }
#endif
    }

private:
    std::intptr_t _value;
};
}

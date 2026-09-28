#pragma once

#include <cstdint>

namespace panda::cuda::detail
{

// Plain data shared with the C++23 facade. CUDA and engine headers stay local to
// their respective translation units.
enum class NativeAvailability : std::uint8_t
{
    Available,
    NoDriver,
    NoDevice,
    Failure
};

struct NativeAvailabilityResult
{
    NativeAvailability availability;
    std::int32_t nativeCode;
};

[[nodiscard]] auto queryNativeAvailability() noexcept -> NativeAvailabilityResult;

}

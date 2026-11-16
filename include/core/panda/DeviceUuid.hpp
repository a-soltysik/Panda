#pragma once

/// @file
/// Stable device identity shared across compatible APIs.

#include <array>
#include <cstddef>
#include <cstdint>

namespace panda
{
/// @brief Number of bytes in a device UUID.
inline constexpr auto deviceUuidSize = std::size_t {16};

/// @brief UUID bytes identifying one physical device across compatible APIs.
using DeviceUuid = std::array<std::uint8_t, deviceUuidSize>;
}

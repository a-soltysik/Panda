#pragma once

/// @file
/// Selected Vulkan device and enabled feature diagnostics.

#include <cstdint>
#include <panda/DeviceUuid.hpp>
#include <string>

namespace panda
{
/// @brief Selected device and driver diagnostics.
struct DeviceInfo
{
    /// Vulkan physical-device name.
    std::string name;
    /// Vulkan physical-device driver name.
    std::string driverName;
    /// Driver-provided human-readable description and version.
    std::string driverInfo;
    /// Driver version as reported by Vulkan; its encoding is vendor-specific.
    std::uint32_t driverVersion {0};
    /// Stable physical-device UUID used to select a matching external API device.
    DeviceUuid deviceUuid {};
    /// Vulkan API version reported by the selected physical device.
    std::uint32_t apiVersion {0};
};
}

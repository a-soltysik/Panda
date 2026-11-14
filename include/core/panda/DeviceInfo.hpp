#pragma once

/// @file
/// Selected Vulkan device and enabled feature diagnostics.

#include <cstdint>
#include <panda/VulkanDeviceUuid.hpp>
#include <string>

namespace panda
{
/// @brief Selected device and enabled core feature diagnostics.
struct DeviceInfo
{
    /// @brief Enabled swapchain-maintenance extension variant.
    enum class SwapchainMaintenance : std::uint8_t
    {
        None,
        Khr,
        Ext
    };

    /// Vulkan physical-device name.
    std::string name;
    /// Vulkan physical-device driver name.
    std::string driverName;
    /// Driver-provided human-readable description and version.
    std::string driverInfo;
    /// Driver version as reported by Vulkan; its encoding is vendor-specific.
    std::uint32_t driverVersion {0};
    /// Stable physical-device identity for compatible external API selection.
    VulkanDeviceUuid deviceUuid {};
    /// True when external buffer memory export was enabled for this Engine.
    /// Individual buffer usages are still checked when an external buffer is created.
    bool externalBufferExportEnabled {false};
    /// True when this Engine can export opaque timeline semaphore handles for external buffers.
    bool externalSemaphoreExportEnabled {false};
    /// Vulkan API version reported by the selected physical device.
    std::uint32_t apiVersion {0};
    /// Graphics and compute queue family selected for ordered work.
    std::uint32_t queueFamily {0};
    /// Present queue family; equals queueFamily when one family supports both.
    std::uint32_t presentQueueFamily {0};
    /// Windowed capability, absent for a context without a surface.
    SwapchainMaintenance swapchainMaintenance {SwapchainMaintenance::None};
};
}

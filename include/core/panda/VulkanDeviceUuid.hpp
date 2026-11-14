#pragma once

/// @file
/// Stable Vulkan physical-device identity.

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace panda
{
/// @brief UUID bytes identifying one selected Vulkan device.
using VulkanDeviceUuid = std::array<std::uint8_t, VK_UUID_SIZE>;
}

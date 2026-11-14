#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <expected>
#include <panda/DeviceInfo.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <vector>

#include "InstanceResources.hpp"

namespace panda::core::vulkan
{
struct SelectedDevice
{
    vk::PhysicalDevice physicalDevice;
    DeviceInfo info;
    vk::PhysicalDeviceLimits limits;
    std::uint32_t timestampValidBits {0};
    float timestampPeriodNanoseconds {0.0F};
};

[[nodiscard]] auto instanceExtensions(const WindowSurface* surface, bool enableValidation = false)
    -> std::expected<InstanceExtensions, Error>;

[[nodiscard]] auto selectDevice(vk::Instance instance, vk::SurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<SelectedDevice, Error>;

}

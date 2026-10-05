#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <expected>
#include <panda/Context.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <vector>

namespace panda::detail
{

struct InstanceExtensions
{
    std::vector<std::string> names;
    bool hasKhrMaintenance {false};
    bool hasExtMaintenance {false};
};

struct SelectedDevice
{
    vk::PhysicalDevice physicalDevice;
    ContextDeviceInfo info;
};

[[nodiscard]] auto instanceExtensions(const WindowSurface* surface, bool enableValidation = false)
    -> std::expected<InstanceExtensions, Error>;

[[nodiscard]] auto selectDevice(vk::Instance instance, VkSurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<SelectedDevice, Error>;

}

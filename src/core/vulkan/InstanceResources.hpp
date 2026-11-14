#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <optional>
#include <string>
#include <vector>

namespace panda::core::vulkan
{
struct InstanceExtensions
{
    std::vector<std::string> names;
    bool hasKhrMaintenance {false};
    bool hasExtMaintenance {false};
};

struct InstanceResources
{
    InstanceExtensions extensions;
    vk::raii::Instance instance {nullptr};
    std::optional<vk::raii::DebugUtilsMessengerEXT> debugMessenger;
    std::optional<vk::raii::SurfaceKHR> surface;
};
}

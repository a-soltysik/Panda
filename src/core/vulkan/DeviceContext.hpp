#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <panda/DeviceInfo.hpp>

#include "InstanceResources.hpp"

namespace panda::core::vulkan
{
// Construction code needs direct access to this data-only owner.
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
struct DeviceContext final
{
    DeviceContext() = default;
    ~DeviceContext() = default;
    DeviceContext(const DeviceContext&) = delete;
    auto operator=(const DeviceContext&) -> DeviceContext& = delete;
    DeviceContext(DeviceContext&&) = delete;
    auto operator=(DeviceContext&&) -> DeviceContext& = delete;

    vk::raii::Context loader {vkGetInstanceProcAddr};
    InstanceResources instance;
    vk::raii::PhysicalDevice physicalDevice {nullptr};
    vk::raii::Device device {nullptr};
    DeviceInfo info;
};

// NOLINTEND(misc-non-private-member-variables-in-classes)
}

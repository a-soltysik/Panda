#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "FakeHandle.hpp"

namespace panda::test
{
inline auto enableRequiredVulkanFeatures(const VkPhysicalDeviceFeatures2& features) -> bool
{
    if (features.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 || features.pNext == nullptr)
    {
        return false;
    }
    auto* const vulkan12 = static_cast<VkPhysicalDeviceVulkan12Features*>(features.pNext);
    if (vulkan12->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES || vulkan12->pNext == nullptr)
    {
        return false;
    }
    auto* const vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
    if (vulkan13->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)
    {
        return false;
    }
    vulkan12->timelineSemaphore = VK_TRUE;
    vulkan13->dynamicRendering = VK_TRUE;
    vulkan13->synchronization2 = VK_TRUE;
    return true;
}

template <typename Function>
auto genericVulkanProc(Function function) noexcept -> PFN_vkVoidFunction
{
    // Vulkan specifies conversion between its entry-point pointer types.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}
}

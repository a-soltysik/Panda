#pragma once

#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "VulkanHpp.hpp"  // IWYU pragma: keep
#include "external/vulkan/VulkanMock.hpp"

namespace panda::test
{
class VulkanTestFixture : public testing::Test
{
protected:
    void TearDown() override
    {
        static_cast<void>(device.release());
        static_cast<void>(instance.release());
    }

    ScopedMock<VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeHandle<VkInstance>(1)};
    VkPhysicalDevice physicalHandle {fakeHandle<VkPhysicalDevice>(2)};
    vk::raii::PhysicalDevice physical {instance, physicalHandle};
    VkDevice deviceHandle {fakeHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
};

inline auto enableRequiredVulkanFeatures(const VkPhysicalDeviceFeatures2& features) -> bool
{
    if (features.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 || features.pNext == nullptr)
    {
        return false;
    }
    const auto* const first = static_cast<const VkBaseInStructure*>(features.pNext);
    if (first->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES || first->pNext == nullptr)
    {
        return false;
    }
    auto* const vulkan12 = static_cast<VkPhysicalDeviceVulkan12Features*>(features.pNext);
    const auto* const second = static_cast<const VkBaseInStructure*>(vulkan12->pNext);
    if (second->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)
    {
        return false;
    }
    auto* const vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
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

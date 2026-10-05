#pragma once

#include <gmock/gmock.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace panda::test
{
class VulkanMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(VkResult, vkEnumerateInstanceVersion, (std::uint32_t*), ());

    MOCK_METHOD(VkResult, vkEnumerateInstanceLayerProperties, (std::uint32_t*, VkLayerProperties*), ());

    MOCK_METHOD(VkResult,
                vkEnumerateInstanceExtensionProperties,
                (const char*, std::uint32_t*, VkExtensionProperties*),
                ());

    MOCK_METHOD(VkResult,
                vkCreateInstance,
                (const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*),
                ());

    MOCK_METHOD(void, vkDestroyInstance, (VkInstance, const VkAllocationCallbacks*), ());

    MOCK_METHOD(PFN_vkVoidFunction, vkGetInstanceProcAddr, (VkInstance, const char*), ());

    MOCK_METHOD(VkResult,
                vkCreateDebugUtilsMessengerEXT,
                (VkInstance,
                 const VkDebugUtilsMessengerCreateInfoEXT*,
                 const VkAllocationCallbacks*,
                 VkDebugUtilsMessengerEXT*),
                ());

    MOCK_METHOD(void,
                vkDestroyDebugUtilsMessengerEXT,
                (VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks*),
                ());

    MOCK_METHOD(VkResult,
                vkEnumerateDeviceExtensionProperties,
                (VkPhysicalDevice, const char*, std::uint32_t*, VkExtensionProperties*),
                ());

    MOCK_METHOD(VkResult, vkEnumeratePhysicalDevices, (VkInstance, std::uint32_t*, VkPhysicalDevice*), ());

    MOCK_METHOD(void, vkGetPhysicalDeviceFeatures2, (VkPhysicalDevice, VkPhysicalDeviceFeatures2*), ());

    MOCK_METHOD(void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice, VkPhysicalDeviceProperties*), ());

    MOCK_METHOD(void,
                vkGetPhysicalDeviceQueueFamilyProperties,
                (VkPhysicalDevice, std::uint32_t*, VkQueueFamilyProperties*),
                ());

    MOCK_METHOD(VkResult,
                vkGetPhysicalDeviceSurfaceFormatsKHR,
                (VkPhysicalDevice, VkSurfaceKHR, std::uint32_t*, VkSurfaceFormatKHR*),
                ());

    MOCK_METHOD(VkResult,
                vkGetPhysicalDeviceSurfacePresentModesKHR,
                (VkPhysicalDevice, VkSurfaceKHR, std::uint32_t*, VkPresentModeKHR*),
                ());

    MOCK_METHOD(VkResult,
                vkGetPhysicalDeviceSurfaceSupportKHR,
                (VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32*),
                ());

    MOCK_METHOD(void, vkGetDeviceQueue, (VkDevice, std::uint32_t, std::uint32_t, VkQueue*), ());

    MOCK_METHOD(PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice, const char*), ());

    MOCK_METHOD(VkResult,
                vkCreateSemaphore,
                (VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*),
                ());

    MOCK_METHOD(VkResult,
                vkCreateCommandPool,
                (VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*),
                ());

    MOCK_METHOD(VkResult,
                vkAllocateCommandBuffers,
                (VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*),
                ());

    MOCK_METHOD(void, vkDestroySemaphore, (VkDevice, VkSemaphore, const VkAllocationCallbacks*), ());

    MOCK_METHOD(void, vkDestroyCommandPool, (VkDevice, VkCommandPool, const VkAllocationCallbacks*), ());

    MOCK_METHOD(void, vkDestroySwapchainKHR, (VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*), ());

    MOCK_METHOD(VkResult, vkResetCommandPool, (VkDevice, VkCommandPool, VkCommandPoolResetFlags), ());

    MOCK_METHOD(VkResult,
                vkAcquireNextImageKHR,
                (VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t*),
                ());

    MOCK_METHOD(VkResult, vkBeginCommandBuffer, (VkCommandBuffer, const VkCommandBufferBeginInfo*), ());

    MOCK_METHOD(void, vkCmdPipelineBarrier2, (VkCommandBuffer, const VkDependencyInfo*), ());

    MOCK_METHOD(void, vkCmdBeginRendering, (VkCommandBuffer, const VkRenderingInfo*), ());

    MOCK_METHOD(void, vkCmdEndRendering, (VkCommandBuffer), ());

    MOCK_METHOD(VkResult, vkEndCommandBuffer, (VkCommandBuffer), ());

    MOCK_METHOD(VkResult, vkQueueSubmit2, (VkQueue, std::uint32_t, const VkSubmitInfo2*, VkFence), ());

    MOCK_METHOD(VkResult, vkQueuePresentKHR, (VkQueue, const VkPresentInfoKHR*), ());

    MOCK_METHOD(VkResult, vkWaitSemaphores, (VkDevice, const VkSemaphoreWaitInfo*, std::uint64_t), ());

    MOCK_METHOD(VkResult, vkReleaseSwapchainImagesKHR, (VkDevice, const VkReleaseSwapchainImagesInfoKHR*), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

extern "C" {
VKAPI_ATTR auto VKAPI_CALL vkReleaseSwapchainImagesKHR(VkDevice device,
                                                       const VkReleaseSwapchainImagesInfoKHR* releaseInfo) -> VkResult;
}

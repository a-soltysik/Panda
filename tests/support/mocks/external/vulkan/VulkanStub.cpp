#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <panda/Assert.hpp>

#include "ScopedMock.hpp"
#include "VulkanMock.hpp"

extern "C" {
VKAPI_ATTR auto VKAPI_CALL vkEnumerateInstanceVersion(std::uint32_t* version) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkEnumerateInstanceVersion(version);
}

VKAPI_ATTR auto VKAPI_CALL vkEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties* properties)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkEnumerateInstanceLayerProperties(
        count,
        properties);
}

VKAPI_ATTR auto VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  std::uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkEnumerateInstanceExtensionProperties(
        pLayerName,
        pPropertyCount,
        pProperties);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkInstance* pInstance) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateInstance(pCreateInfo,
                                                                                              pAllocator,
                                                                                              pInstance);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyInstance(instance, pAllocator);
}

VKAPI_ATTR auto VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) -> PFN_vkVoidFunction
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance,
                                                          const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                          const VkAllocationCallbacks* pAllocator,
                                                          VkDebugUtilsMessengerEXT* pMessenger) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateDebugUtilsMessengerEXT(instance,
                                                                                                            pCreateInfo,
                                                                                                            pAllocator,
                                                                                                            pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance,
                                                           VkDebugUtilsMessengerEXT messenger,
                                                           const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyDebugUtilsMessengerEXT(instance,
                                                                                                      messenger,
                                                                                                      pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device,
                                            std::uint32_t queueFamilyIndex,
                                            std::uint32_t queueIndex,
                                            VkQueue* pQueue)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetDeviceQueue(device,
                                                                                       queueFamilyIndex,
                                                                                       queueIndex,
                                                                                       pQueue);
}

VKAPI_ATTR auto VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) -> PFN_vkVoidFunction
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetDeviceProcAddr(device, pName);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateSemaphore(VkDevice device,
                                             const VkSemaphoreCreateInfo* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkSemaphore* pSemaphore) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateSemaphore(device,
                                                                                               pCreateInfo,
                                                                                               pAllocator,
                                                                                               pSemaphore);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateCommandPool(VkDevice device,
                                               const VkCommandPoolCreateInfo* pCreateInfo,
                                               const VkAllocationCallbacks* pAllocator,
                                               VkCommandPool* pCommandPool) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateCommandPool(device,
                                                                                                 pCreateInfo,
                                                                                                 pAllocator,
                                                                                                 pCommandPool);
}

VKAPI_ATTR auto VKAPI_CALL vkAllocateCommandBuffers(VkDevice device,
                                                    const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                    VkCommandBuffer* pCommandBuffers) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkAllocateCommandBuffers(device,
                                                                                                      pAllocateInfo,
                                                                                                      pCommandBuffers);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device,
                                              VkSemaphore semaphore,
                                              const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroySemaphore(device, semaphore, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device,
                                                VkCommandPool commandPool,
                                                const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyCommandPool(device,
                                                                                           commandPool,
                                                                                           pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device,
                                                 VkSwapchainKHR swapchain,
                                                 const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroySwapchainKHR(device,
                                                                                            swapchain,
                                                                                            pAllocator);
}

VKAPI_ATTR auto VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkResetCommandPool(device,
                                                                                                commandPool,
                                                                                                flags);
}

VKAPI_ATTR auto VKAPI_CALL vkAcquireNextImageKHR(VkDevice device,
                                                 VkSwapchainKHR swapchain,
                                                 std::uint64_t timeout,
                                                 VkSemaphore semaphore,
                                                 VkFence fence,
                                                 std::uint32_t* imageIndex) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, imageIndex);
}

VKAPI_ATTR auto VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                const VkCommandBufferBeginInfo* beginInfo) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkBeginCommandBuffer(commandBuffer,
                                                                                                  beginInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* dependencyInfo)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdPipelineBarrier2(commandBuffer,
                                                                                            dependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* renderingInfo)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdBeginRendering(commandBuffer, renderingInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdEndRendering(commandBuffer);
}

VKAPI_ATTR auto VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkEndCommandBuffer(commandBuffer);
}

VKAPI_ATTR auto VKAPI_CALL vkQueueSubmit2(VkQueue queue,
                                          std::uint32_t submitCount,
                                          const VkSubmitInfo2* submits,
                                          VkFence fence) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkQueueSubmit2(queue,
                                                                                            submitCount,
                                                                                            submits,
                                                                                            fence);
}

VKAPI_ATTR auto VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkQueuePresentKHR(queue, presentInfo);
}

VKAPI_ATTR auto VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* waitInfo, std::uint64_t timeout)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkWaitSemaphores(device,
                                                                                              waitInfo,
                                                                                              timeout);
}

VKAPI_ATTR auto VKAPI_CALL vkReleaseSwapchainImagesKHR(VkDevice device,
                                                       const VkReleaseSwapchainImagesInfoKHR* releaseInfo) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkReleaseSwapchainImagesKHR(device,
                                                                                                         releaseInfo);
}

// All other Vulkan entry points used by Core terminate a unit test if reached.
// Extend VulkanMock and replace the corresponding stub when a scenario needs one.
VKAPI_ATTR auto VKAPI_CALL vkCreateDevice([[maybe_unused]] VkPhysicalDevice physicalDevice,
                                          [[maybe_unused]] const VkDeviceCreateInfo* pCreateInfo,
                                          [[maybe_unused]] const VkAllocationCallbacks* pAllocator,
                                          [[maybe_unused]] VkDevice* pDevice) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkCreateDevice");
}

VKAPI_ATTR auto VKAPI_CALL vkCreateFence([[maybe_unused]] VkDevice device,
                                         [[maybe_unused]] const VkFenceCreateInfo* pCreateInfo,
                                         [[maybe_unused]] const VkAllocationCallbacks* pAllocator,
                                         [[maybe_unused]] VkFence* pFence) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkCreateFence");
}

VKAPI_ATTR auto VKAPI_CALL vkCreateImageView([[maybe_unused]] VkDevice device,
                                             [[maybe_unused]] const VkImageViewCreateInfo* pCreateInfo,
                                             [[maybe_unused]] const VkAllocationCallbacks* pAllocator,
                                             [[maybe_unused]] VkImageView* pView) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkCreateImageView");
}

VKAPI_ATTR auto VKAPI_CALL vkCreateSwapchainKHR([[maybe_unused]] VkDevice device,
                                                [[maybe_unused]] const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                [[maybe_unused]] const VkAllocationCallbacks* pAllocator,
                                                [[maybe_unused]] VkSwapchainKHR* pSwapchain) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkCreateSwapchainKHR");
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice([[maybe_unused]] VkDevice device,
                                           [[maybe_unused]] const VkAllocationCallbacks* pAllocator)
{
    panda::panic("Unexpected Vulkan call: vkDestroyDevice");
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence([[maybe_unused]] VkDevice device,
                                          [[maybe_unused]] VkFence fence,
                                          [[maybe_unused]] const VkAllocationCallbacks* pAllocator)
{
    panda::panic("Unexpected Vulkan call: vkDestroyFence");
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView([[maybe_unused]] VkDevice device,
                                              [[maybe_unused]] VkImageView imageView,
                                              [[maybe_unused]] const VkAllocationCallbacks* pAllocator)
{
    panda::panic("Unexpected Vulkan call: vkDestroyImageView");
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR([[maybe_unused]] VkInstance instance,
                                               [[maybe_unused]] VkSurfaceKHR surface,
                                               [[maybe_unused]] const VkAllocationCallbacks* pAllocator)
{
    panda::panic("Unexpected Vulkan call: vkDestroySurfaceKHR");
}

VKAPI_ATTR auto VKAPI_CALL vkEnumerateDeviceExtensionProperties([[maybe_unused]] VkPhysicalDevice physicalDevice,
                                                                [[maybe_unused]] const char* pLayerName,
                                                                uint32_t* pPropertyCount,
                                                                VkExtensionProperties* pProperties) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

VKAPI_ATTR auto VKAPI_CALL vkEnumeratePhysicalDevices([[maybe_unused]] VkInstance instance,
                                                      uint32_t* pPhysicalDeviceCount,
                                                      VkPhysicalDevice* pPhysicalDevices) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkEnumeratePhysicalDevices(
        instance,
        pPhysicalDeviceCount,
        pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                        VkPhysicalDeviceFeatures2* pFeatures)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceFeatures2(physicalDevice,
                                                                                                   pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceProperties* pProperties)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceProperties(physicalDevice,
                                                                                                    pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                                                    uint32_t* pQueueFamilyPropertyCount,
                                                                    VkQueueFamilyProperties* pQueueFamilyProperties)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice,
        pQueueFamilyPropertyCount,
        pQueueFamilyProperties);
}

VKAPI_ATTR auto VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR([[maybe_unused]] VkPhysicalDevice physicalDevice,
                                          [[maybe_unused]] VkSurfaceKHR surface,
                                          [[maybe_unused]] VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
}

VKAPI_ATTR auto VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                                                VkSurfaceKHR surface,
                                                                uint32_t* pSurfaceFormatCount,
                                                                VkSurfaceFormatKHR* pSurfaceFormats) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR auto VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                                                     VkSurfaceKHR surface,
                                                                     uint32_t* pPresentModeCount,
                                                                     VkPresentModeKHR* pPresentModes) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

VKAPI_ATTR auto VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                                                uint32_t queueFamilyIndex,
                                                                VkSurfaceKHR surface,
                                                                VkBool32* pSupported) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
}

VKAPI_ATTR auto VKAPI_CALL vkGetSwapchainImagesKHR([[maybe_unused]] VkDevice device,
                                                   [[maybe_unused]] VkSwapchainKHR swapchain,
                                                   [[maybe_unused]] uint32_t* pSwapchainImageCount,
                                                   [[maybe_unused]] VkImage* pSwapchainImages) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkGetSwapchainImagesKHR");
}

VKAPI_ATTR auto VKAPI_CALL vkResetFences([[maybe_unused]] VkDevice device,
                                         [[maybe_unused]] uint32_t fenceCount,
                                         [[maybe_unused]] const VkFence* pFences) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkResetFences");
}

VKAPI_ATTR auto VKAPI_CALL vkWaitForFences([[maybe_unused]] VkDevice device,
                                           [[maybe_unused]] uint32_t fenceCount,
                                           [[maybe_unused]] const VkFence* pFences,
                                           [[maybe_unused]] VkBool32 waitAll,
                                           [[maybe_unused]] uint64_t timeout) -> VkResult
{
    panda::panic("Unexpected Vulkan call: vkWaitForFences");
}
}

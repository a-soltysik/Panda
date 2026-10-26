#include <gmock/gmock.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "ScopedMock.hpp"
#include "VulkanMock.hpp"
#include "VulkanTestSupport.hpp"

extern "C" {
VKAPI_ATTR auto VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, std::uint64_t* pValue)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetSemaphoreCounterValue(device,
                                                                                                        semaphore,
                                                                                                        pValue);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateBuffer(VkDevice device,
                                          const VkBufferCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkBuffer* pBuffer) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateBuffer(device,
                                                                                            pCreateInfo,
                                                                                            pAllocator,
                                                                                            pBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyBuffer(device, buffer, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device,
                                                          const VkBufferMemoryRequirementsInfo2* pInfo,
                                                          VkMemoryRequirements2* pMemoryRequirements)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetBufferMemoryRequirements2(
        device,
        pInfo,
        pMemoryRequirements);
}

VKAPI_ATTR auto VKAPI_CALL vkBindBufferMemory2(VkDevice device,
                                               std::uint32_t bindInfoCount,
                                               const VkBindBufferMemoryInfo* pBindInfos) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkBindBufferMemory2(device,
                                                                                                 bindInfoCount,
                                                                                                 pBindInfos);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateImage(VkDevice device,
                                         const VkImageCreateInfo* pCreateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkImage* pImage) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateImage(device,
                                                                                           pCreateInfo,
                                                                                           pAllocator,
                                                                                           pImage);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateQueryPool(VkDevice device,
                                             const VkQueryPoolCreateInfo* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkQueryPool* pQueryPool) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateQueryPool(device,
                                                                                               pCreateInfo,
                                                                                               pAllocator,
                                                                                               pQueryPool);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device,
                                              VkQueryPool queryPool,
                                              const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyQueryPool(device, queryPool, pAllocator);
}

// Vulkan fixes this entry point's eight-parameter C signature.
// NOLINTNEXTLINE(readability-function-size)
VKAPI_ATTR auto VKAPI_CALL vkGetQueryPoolResults(VkDevice device,
                                                 VkQueryPool queryPool,
                                                 std::uint32_t firstQuery,
                                                 std::uint32_t queryCount,
                                                 std::size_t dataSize,
                                                 void* pData,
                                                 VkDeviceSize stride,
                                                 VkQueryResultFlags flags) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetQueryPoolResults(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyImage(device, image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device,
                                                         const VkImageMemoryRequirementsInfo2* pInfo,
                                                         VkMemoryRequirements2* pMemoryRequirements)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetImageMemoryRequirements2(
        device,
        pInfo,
        pMemoryRequirements);
}

VKAPI_ATTR auto VKAPI_CALL vkBindImageMemory2(VkDevice device,
                                              std::uint32_t bindInfoCount,
                                              const VkBindImageMemoryInfo* pBindInfos) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkBindImageMemory2(device,
                                                                                                bindInfoCount,
                                                                                                pBindInfos);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer srcBuffer,
                                           VkBuffer dstBuffer,
                                           std::uint32_t regionCount,
                                           const VkBufferCopy* pRegions)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdCopyBuffer(commandBuffer,
                                                                                      srcBuffer,
                                                                                      dstBuffer,
                                                                                      regionCount,
                                                                                      pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                  VkBuffer srcBuffer,
                                                  VkImage dstImage,
                                                  VkImageLayout dstImageLayout,
                                                  std::uint32_t regionCount,
                                                  const VkBufferImageCopy* pRegions)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* dependencyInfo)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdPipelineBarrier2(commandBuffer,
                                                                                            dependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer,
                                               VkQueryPool queryPool,
                                               std::uint32_t firstQuery,
                                               std::uint32_t queryCount)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdResetQueryPool(commandBuffer,
                                                                                          queryPool,
                                                                                          firstQuery,
                                                                                          queryCount);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                                                VkPipelineStageFlags2 pipelineStage,
                                                VkQueryPool queryPool,
                                                std::uint32_t query)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCmdWriteTimestamp2(commandBuffer,
                                                                                           pipelineStage,
                                                                                           queryPool,
                                                                                           query);
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

VKAPI_ATTR auto VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice,
                                          const VkDeviceCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkDevice* pDevice) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateDevice(physicalDevice,
                                                                                            pCreateInfo,
                                                                                            pAllocator,
                                                                                            pDevice);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateFence(VkDevice device,
                                         const VkFenceCreateInfo* pCreateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkFence* pFence) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateFence(device,
                                                                                           pCreateInfo,
                                                                                           pAllocator,
                                                                                           pFence);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateImageView(VkDevice device,
                                             const VkImageViewCreateInfo* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkImageView* pView) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateImageView(device,
                                                                                               pCreateInfo,
                                                                                               pAllocator,
                                                                                               pView);
}

VKAPI_ATTR auto VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,
                                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkSwapchainKHR* pSwapchain) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkCreateSwapchainKHR(device,
                                                                                                  pCreateInfo,
                                                                                                  pAllocator,
                                                                                                  pSwapchain);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyDevice(device, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyFence(device, fence, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device,
                                              VkImageView imageView,
                                              const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroyImageView(device, imageView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance,
                                               VkSurfaceKHR surface,
                                               const VkAllocationCallbacks* pAllocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkDestroySurfaceKHR(instance,
                                                                                          surface,
                                                                                          pAllocator);
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

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice,
                                               const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
                                               VkExternalSemaphoreProperties* pExternalSemaphoreProperties)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceExternalSemaphoreProperties(
        physicalDevice,
        pExternalSemaphoreInfo,
        pExternalSemaphoreProperties);
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

VKAPI_ATTR auto VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                                                     VkSurfaceKHR surface,
                                                                     VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physicalDevice,
        surface,
        pSurfaceCapabilities);
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

VKAPI_ATTR auto VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device,
                                                   VkSwapchainKHR swapchain,
                                                   uint32_t* pSwapchainImageCount,
                                                   VkImage* pSwapchainImages) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VKAPI_ATTR auto VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkResetFences(device, fenceCount, pFences);
}

VKAPI_ATTR auto VKAPI_CALL vkWaitForFences(
    VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkWaitForFences(device,
                                                                                             fenceCount,
                                                                                             pFences,
                                                                                             waitAll,
                                                                                             timeout);
}
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice device,
                                                          VkPhysicalDeviceProperties2* properties)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceProperties2(device,
                                                                                                     properties);
}

// Vulkan fixes this entry point's seven-parameter C signature.
// NOLINTNEXTLINE(readability-function-size)
VKAPI_ATTR auto VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice,
                                                                    VkFormat format,
                                                                    VkImageType type,
                                                                    VkImageTiling tiling,
                                                                    VkImageUsageFlags usage,
                                                                    VkImageCreateFlags flags,
                                                                    VkImageFormatProperties* properties) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkGetPhysicalDeviceImageFormatProperties(physicalDevice, format, type, tiling, usage, flags, properties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice device,
                                                               VkPhysicalDeviceMemoryProperties* properties)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkGetPhysicalDeviceMemoryProperties(device,
                                                                                                          properties);
}

VKAPI_ATTR auto VKAPI_CALL vkAllocateMemory(VkDevice device,
                                            const VkMemoryAllocateInfo* info,
                                            const VkAllocationCallbacks* allocator,
                                            VkDeviceMemory* memory) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkAllocateMemory(device,
                                                                                              info,
                                                                                              allocator,
                                                                                              memory);
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkFreeMemory(device, memory, allocator);
}

VKAPI_ATTR auto VKAPI_CALL vkMapMemory(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** data)
    -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock()
        .vkMapMemory(device, memory, offset, size, flags, data);
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkUnmapMemory(device, memory);
}

VKAPI_ATTR auto VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device,
                                                     std::uint32_t count,
                                                     const VkMappedMemoryRange* ranges) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkFlushMappedMemoryRanges(device,
                                                                                                       count,
                                                                                                       ranges);
}

VKAPI_ATTR auto VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device,
                                                          std::uint32_t count,
                                                          const VkMappedMemoryRange* ranges) -> VkResult
{
    return panda::test::ScopedMock<panda::test::VulkanMock>::getActiveMock().vkInvalidateMappedMemoryRanges(device,
                                                                                                            count,
                                                                                                            ranges);
}

namespace panda::test
{
namespace
{
struct EntryPoint
{
    std::string_view name;
    PFN_vkVoidFunction function;
};

const auto entryPoints = std::array {
    EntryPoint {.name = "vkCmdCopyBuffer",                                .function = genericVulkanProc(&vkCmdCopyBuffer)              },
    EntryPoint {.name = "vkGetSemaphoreCounterValue",                     .function = genericVulkanProc(&vkGetSemaphoreCounterValue)   },
    EntryPoint {.name = "vkCreateBuffer",                                 .function = genericVulkanProc(&vkCreateBuffer)               },
    EntryPoint {.name = "vkDestroyBuffer",                                .function = genericVulkanProc(&vkDestroyBuffer)              },
    EntryPoint {.name = "vkGetBufferMemoryRequirements2",
                .function = genericVulkanProc(&vkGetBufferMemoryRequirements2)                                                         },
    EntryPoint {.name = "vkBindBufferMemory2",                            .function = genericVulkanProc(&vkBindBufferMemory2)          },
    EntryPoint {.name = "vkCreateImage",                                  .function = genericVulkanProc(&vkCreateImage)                },
    EntryPoint {.name = "vkDestroyImage",                                 .function = genericVulkanProc(&vkDestroyImage)               },
    EntryPoint {.name = "vkGetImageMemoryRequirements2",                  .function = genericVulkanProc(&vkGetImageMemoryRequirements2)},
    EntryPoint {.name = "vkBindImageMemory2",                             .function = genericVulkanProc(&vkBindImageMemory2)           },
    EntryPoint {.name = "vkAllocateMemory",                               .function = genericVulkanProc(&vkAllocateMemory)             },
    EntryPoint {.name = "vkFreeMemory",                                   .function = genericVulkanProc(&vkFreeMemory)                 },
    EntryPoint {.name = "vkMapMemory",                                    .function = genericVulkanProc(&vkMapMemory)                  },
    EntryPoint {.name = "vkUnmapMemory",                                  .function = genericVulkanProc(&vkUnmapMemory)                },
    EntryPoint {.name = "vkFlushMappedMemoryRanges",                      .function = genericVulkanProc(&vkFlushMappedMemoryRanges)    },
    EntryPoint {.name = "vkInvalidateMappedMemoryRanges",
                .function = genericVulkanProc(&vkInvalidateMappedMemoryRanges)                                                         },
    EntryPoint {.name = "vkGetPhysicalDeviceProperties2",
                .function = genericVulkanProc(&vkGetPhysicalDeviceProperties2)                                                         },
    EntryPoint {.name = "vkGetPhysicalDeviceImageFormatProperties",
                .function = genericVulkanProc(&vkGetPhysicalDeviceImageFormatProperties)                                               },
    EntryPoint {.name = "vkGetPhysicalDeviceMemoryProperties",
                .function = genericVulkanProc(&vkGetPhysicalDeviceMemoryProperties)                                                    },
    EntryPoint {.name = "vkEnumerateInstanceVersion",                     .function = genericVulkanProc(&vkEnumerateInstanceVersion)   },
    EntryPoint {.name = "vkEnumerateInstanceLayerProperties",
                .function = genericVulkanProc(&vkEnumerateInstanceLayerProperties)                                                     },
    EntryPoint {.name = "vkEnumerateInstanceExtensionProperties",
                .function = genericVulkanProc(&vkEnumerateInstanceExtensionProperties)                                                 },
    EntryPoint {.name = "vkCreateInstance",                               .function = genericVulkanProc(&vkCreateInstance)             },
    EntryPoint {.name = "vkDestroyInstance",                              .function = genericVulkanProc(&vkDestroyInstance)            },
    EntryPoint {.name = "vkGetInstanceProcAddr",                          .function = genericVulkanProc(&vkGetInstanceProcAddr)        },
    EntryPoint {.name = "vkCreateDebugUtilsMessengerEXT",
                .function = genericVulkanProc(&vkCreateDebugUtilsMessengerEXT)                                                         },
    EntryPoint {.name = "vkDestroyDebugUtilsMessengerEXT",
                .function = genericVulkanProc(&vkDestroyDebugUtilsMessengerEXT)                                                        },
    EntryPoint {.name = "vkGetDeviceQueue",                               .function = genericVulkanProc(&vkGetDeviceQueue)             },
    EntryPoint {.name = "vkGetDeviceProcAddr",                            .function = genericVulkanProc(&vkGetDeviceProcAddr)          },
    EntryPoint {.name = "vkCreateSemaphore",                              .function = genericVulkanProc(&vkCreateSemaphore)            },
    EntryPoint {.name = "vkCreateQueryPool",                              .function = genericVulkanProc(&vkCreateQueryPool)            },
    EntryPoint {.name = "vkDestroyQueryPool",                             .function = genericVulkanProc(&vkDestroyQueryPool)           },
    EntryPoint {.name = "vkGetQueryPoolResults",                          .function = genericVulkanProc(&vkGetQueryPoolResults)        },
    EntryPoint {.name = "vkCreateCommandPool",                            .function = genericVulkanProc(&vkCreateCommandPool)          },
    EntryPoint {.name = "vkAllocateCommandBuffers",                       .function = genericVulkanProc(&vkAllocateCommandBuffers)     },
    EntryPoint {.name = "vkDestroySemaphore",                             .function = genericVulkanProc(&vkDestroySemaphore)           },
    EntryPoint {.name = "vkDestroyCommandPool",                           .function = genericVulkanProc(&vkDestroyCommandPool)         },
    EntryPoint {.name = "vkDestroySwapchainKHR",                          .function = genericVulkanProc(&vkDestroySwapchainKHR)        },
    EntryPoint {.name = "vkResetCommandPool",                             .function = genericVulkanProc(&vkResetCommandPool)           },
    EntryPoint {.name = "vkAcquireNextImageKHR",                          .function = genericVulkanProc(&vkAcquireNextImageKHR)        },
    EntryPoint {.name = "vkBeginCommandBuffer",                           .function = genericVulkanProc(&vkBeginCommandBuffer)         },
    EntryPoint {.name = "vkCmdPipelineBarrier2",                          .function = genericVulkanProc(&vkCmdPipelineBarrier2)        },
    EntryPoint {.name = "vkCmdResetQueryPool",                            .function = genericVulkanProc(&vkCmdResetQueryPool)          },
    EntryPoint {.name = "vkCmdWriteTimestamp2",                           .function = genericVulkanProc(&vkCmdWriteTimestamp2)         },
    EntryPoint {.name = "vkCmdCopyBufferToImage",                         .function = genericVulkanProc(&vkCmdCopyBufferToImage)       },
    EntryPoint {.name = "vkCmdBeginRendering",                            .function = genericVulkanProc(&vkCmdBeginRendering)          },
    EntryPoint {.name = "vkCmdEndRendering",                              .function = genericVulkanProc(&vkCmdEndRendering)            },
    EntryPoint {.name = "vkEndCommandBuffer",                             .function = genericVulkanProc(&vkEndCommandBuffer)           },
    EntryPoint {.name = "vkQueueSubmit2",                                 .function = genericVulkanProc(&vkQueueSubmit2)               },
    EntryPoint {.name = "vkQueuePresentKHR",                              .function = genericVulkanProc(&vkQueuePresentKHR)            },
    EntryPoint {.name = "vkWaitSemaphores",                               .function = genericVulkanProc(&vkWaitSemaphores)             },
    EntryPoint {.name = "vkReleaseSwapchainImagesKHR",                    .function = genericVulkanProc(&vkReleaseSwapchainImagesKHR)  },
    EntryPoint {.name = "vkCreateDevice",                                 .function = genericVulkanProc(&vkCreateDevice)               },
    EntryPoint {.name = "vkCreateFence",                                  .function = genericVulkanProc(&vkCreateFence)                },
    EntryPoint {.name = "vkCreateImageView",                              .function = genericVulkanProc(&vkCreateImageView)            },
    EntryPoint {.name = "vkCreateSwapchainKHR",                           .function = genericVulkanProc(&vkCreateSwapchainKHR)         },
    EntryPoint {.name = "vkDestroyDevice",                                .function = genericVulkanProc(&vkDestroyDevice)              },
    EntryPoint {.name = "vkDestroyFence",                                 .function = genericVulkanProc(&vkDestroyFence)               },
    EntryPoint {.name = "vkDestroyImageView",                             .function = genericVulkanProc(&vkDestroyImageView)           },
    EntryPoint {.name = "vkDestroySurfaceKHR",                            .function = genericVulkanProc(&vkDestroySurfaceKHR)          },
    EntryPoint {.name = "vkEnumerateDeviceExtensionProperties",
                .function = genericVulkanProc(&vkEnumerateDeviceExtensionProperties)                                                   },
    EntryPoint {.name = "vkEnumeratePhysicalDevices",                     .function = genericVulkanProc(&vkEnumeratePhysicalDevices)   },
    EntryPoint {.name = "vkGetPhysicalDeviceFeatures2",                   .function = genericVulkanProc(&vkGetPhysicalDeviceFeatures2) },
    EntryPoint {.name = "vkGetPhysicalDeviceExternalSemaphoreProperties",
                .function = genericVulkanProc(&vkGetPhysicalDeviceExternalSemaphoreProperties)                                         },
    EntryPoint {.name = "vkGetPhysicalDeviceProperties",                  .function = genericVulkanProc(&vkGetPhysicalDeviceProperties)},
    EntryPoint {.name = "vkGetPhysicalDeviceQueueFamilyProperties",
                .function = genericVulkanProc(&vkGetPhysicalDeviceQueueFamilyProperties)                                               },
    EntryPoint {.name = "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                .function = genericVulkanProc(&vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                              },
    EntryPoint {.name = "vkGetPhysicalDeviceSurfaceFormatsKHR",
                .function = genericVulkanProc(&vkGetPhysicalDeviceSurfaceFormatsKHR)                                                   },
    EntryPoint {.name = "vkGetPhysicalDeviceSurfacePresentModesKHR",
                .function = genericVulkanProc(&vkGetPhysicalDeviceSurfacePresentModesKHR)                                              },
    EntryPoint {.name = "vkGetPhysicalDeviceSurfaceSupportKHR",
                .function = genericVulkanProc(&vkGetPhysicalDeviceSurfaceSupportKHR)                                                   },
    EntryPoint {.name = "vkGetSwapchainImagesKHR",                        .function = genericVulkanProc(&vkGetSwapchainImagesKHR)      },
    EntryPoint {.name = "vkResetFences",                                  .function = genericVulkanProc(&vkResetFences)                },
    EntryPoint {.name = "vkWaitForFences",                                .function = genericVulkanProc(&vkWaitForFences)              }
};

auto resolveEntryPoint(std::string_view name) -> PFN_vkVoidFunction
{
    const auto* const found = std::to_address(std::ranges::find(entryPoints, name, &EntryPoint::name));
    return found == std::to_address(entryPoints.end()) ? nullptr : found->function;
}
}

VulkanMock::VulkanMock()
{
    EXPECT_CALL(*this, vkGetInstanceProcAddr(testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly([](VkInstance, const char* name) {
            return resolveEntryPoint(name);
        });
    EXPECT_CALL(*this, vkGetDeviceProcAddr(testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly([](VkDevice, const char* name) {
            return resolveEntryPoint(name);
        });
}
}

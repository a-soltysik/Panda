#include <gmock/gmock.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

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

namespace panda::test
{
namespace
{
template <typename Function>
auto genericProc(Function function) noexcept -> PFN_vkVoidFunction
{
    // Vulkan specifies conversion between its entry-point pointer types.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

struct EntryPoint
{
    std::string_view name;
    PFN_vkVoidFunction function;
};

const auto entryPoints = std::to_array<EntryPoint>({
    {.name = "vkEnumerateInstanceVersion",                .function = genericProc(&vkEnumerateInstanceVersion)          },
    {.name = "vkEnumerateInstanceLayerProperties",        .function = genericProc(&vkEnumerateInstanceLayerProperties)  },
    {.name = "vkEnumerateInstanceExtensionProperties",
     .function = genericProc(&vkEnumerateInstanceExtensionProperties)                                                   },
    {.name = "vkCreateInstance",                          .function = genericProc(&vkCreateInstance)                    },
    {.name = "vkDestroyInstance",                         .function = genericProc(&vkDestroyInstance)                   },
    {.name = "vkGetInstanceProcAddr",                     .function = genericProc(&vkGetInstanceProcAddr)               },
    {.name = "vkCreateDebugUtilsMessengerEXT",            .function = genericProc(&vkCreateDebugUtilsMessengerEXT)      },
    {.name = "vkDestroyDebugUtilsMessengerEXT",           .function = genericProc(&vkDestroyDebugUtilsMessengerEXT)     },
    {.name = "vkGetDeviceQueue",                          .function = genericProc(&vkGetDeviceQueue)                    },
    {.name = "vkGetDeviceProcAddr",                       .function = genericProc(&vkGetDeviceProcAddr)                 },
    {.name = "vkCreateSemaphore",                         .function = genericProc(&vkCreateSemaphore)                   },
    {.name = "vkCreateCommandPool",                       .function = genericProc(&vkCreateCommandPool)                 },
    {.name = "vkAllocateCommandBuffers",                  .function = genericProc(&vkAllocateCommandBuffers)            },
    {.name = "vkDestroySemaphore",                        .function = genericProc(&vkDestroySemaphore)                  },
    {.name = "vkDestroyCommandPool",                      .function = genericProc(&vkDestroyCommandPool)                },
    {.name = "vkDestroySwapchainKHR",                     .function = genericProc(&vkDestroySwapchainKHR)               },
    {.name = "vkResetCommandPool",                        .function = genericProc(&vkResetCommandPool)                  },
    {.name = "vkAcquireNextImageKHR",                     .function = genericProc(&vkAcquireNextImageKHR)               },
    {.name = "vkBeginCommandBuffer",                      .function = genericProc(&vkBeginCommandBuffer)                },
    {.name = "vkCmdPipelineBarrier2",                     .function = genericProc(&vkCmdPipelineBarrier2)               },
    {.name = "vkCmdBeginRendering",                       .function = genericProc(&vkCmdBeginRendering)                 },
    {.name = "vkCmdEndRendering",                         .function = genericProc(&vkCmdEndRendering)                   },
    {.name = "vkEndCommandBuffer",                        .function = genericProc(&vkEndCommandBuffer)                  },
    {.name = "vkQueueSubmit2",                            .function = genericProc(&vkQueueSubmit2)                      },
    {.name = "vkQueuePresentKHR",                         .function = genericProc(&vkQueuePresentKHR)                   },
    {.name = "vkWaitSemaphores",                          .function = genericProc(&vkWaitSemaphores)                    },
    {.name = "vkReleaseSwapchainImagesKHR",               .function = genericProc(&vkReleaseSwapchainImagesKHR)         },
    {.name = "vkCreateDevice",                            .function = genericProc(&vkCreateDevice)                      },
    {.name = "vkCreateFence",                             .function = genericProc(&vkCreateFence)                       },
    {.name = "vkCreateImageView",                         .function = genericProc(&vkCreateImageView)                   },
    {.name = "vkCreateSwapchainKHR",                      .function = genericProc(&vkCreateSwapchainKHR)                },
    {.name = "vkDestroyDevice",                           .function = genericProc(&vkDestroyDevice)                     },
    {.name = "vkDestroyFence",                            .function = genericProc(&vkDestroyFence)                      },
    {.name = "vkDestroyImageView",                        .function = genericProc(&vkDestroyImageView)                  },
    {.name = "vkDestroySurfaceKHR",                       .function = genericProc(&vkDestroySurfaceKHR)                 },
    {.name = "vkEnumerateDeviceExtensionProperties",      .function = genericProc(&vkEnumerateDeviceExtensionProperties)},
    {.name = "vkEnumeratePhysicalDevices",                .function = genericProc(&vkEnumeratePhysicalDevices)          },
    {.name = "vkGetPhysicalDeviceFeatures2",              .function = genericProc(&vkGetPhysicalDeviceFeatures2)        },
    {.name = "vkGetPhysicalDeviceProperties",             .function = genericProc(&vkGetPhysicalDeviceProperties)       },
    {.name = "vkGetPhysicalDeviceQueueFamilyProperties",
     .function = genericProc(&vkGetPhysicalDeviceQueueFamilyProperties)                                                 },
    {.name = "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
     .function = genericProc(&vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                },
    {.name = "vkGetPhysicalDeviceSurfaceFormatsKHR",      .function = genericProc(&vkGetPhysicalDeviceSurfaceFormatsKHR)},
    {.name = "vkGetPhysicalDeviceSurfacePresentModesKHR",
     .function = genericProc(&vkGetPhysicalDeviceSurfacePresentModesKHR)                                                },
    {.name = "vkGetPhysicalDeviceSurfaceSupportKHR",      .function = genericProc(&vkGetPhysicalDeviceSurfaceSupportKHR)},
    {.name = "vkGetSwapchainImagesKHR",                   .function = genericProc(&vkGetSwapchainImagesKHR)             },
    {.name = "vkResetFences",                             .function = genericProc(&vkResetFences)                       },
    {.name = "vkWaitForFences",                           .function = genericProc(&vkWaitForFences)                     }
});

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

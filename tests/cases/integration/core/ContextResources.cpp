// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto invalidMemoryIntent() -> panda::BufferMemory
{
    // Deliberate invalid input to verify validation before native creation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<panda::BufferMemory>(255);
}

auto extension(std::string_view name) -> VkExtensionProperties
{
    auto value = VkExtensionProperties {};
    std::ranges::copy(name, std::span {value.extensionName}.begin());
    return value;
}

auto queueFamily(VkQueueFlags flags) -> VkQueueFamilyProperties
{
    return {
        .queueFlags = flags,
        .queueCount = 1,
        .timestampValidBits = 0,
        .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
    };
}

auto populateQueueFamilies(bool separate, std::uint32_t* count, VkQueueFamilyProperties* output) -> VkResult
{
    *count = separate ? 2U : 1U;
    if (output != nullptr)
    {
        auto families = std::span {output, *count};
        families.front() =
            queueFamily(static_cast<VkQueueFlags>(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
        if (separate)
        {
            families.back() = queueFamily(static_cast<VkQueueFlags>(vk::QueueFlagBits::eTransfer));
        }
    }
    return success;
}

void verifyRequiredDeviceFeatures(const VkPhysicalDeviceVulkan12Features& vulkan12,
                                  const VkPhysicalDeviceVulkan13Features& vulkan13)
{
    EXPECT_EQ(vulkan12.timelineSemaphore, vk::True);
    EXPECT_EQ(vulkan13.dynamicRendering, vk::True);
    EXPECT_EQ(vulkan13.synchronization2, vk::True);
}

void verifyDeviceFeatures(const VkDeviceCreateInfo& info, bool windowed)
{
    ASSERT_NE(info.pNext, nullptr);
    const auto* const features = static_cast<const VkPhysicalDeviceFeatures2*>(info.pNext);
    ASSERT_NE(features->pNext, nullptr);
    const auto* const vulkan12 = static_cast<const VkPhysicalDeviceVulkan12Features*>(features->pNext);
    ASSERT_NE(vulkan12->pNext, nullptr);
    const auto* const vulkan13 = static_cast<const VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
    verifyRequiredDeviceFeatures(*vulkan12, *vulkan13);
    if (windowed)
    {
        ASSERT_NE(vulkan13->pNext, nullptr);
        const auto* const maintenance =
            static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
        EXPECT_EQ(maintenance->swapchainMaintenance1, vk::True);
    }
    else
    {
        EXPECT_EQ(vulkan13->pNext, nullptr);
    }
}

void verifyImageIdentity(const VkImageCreateInfo& info, const panda::TextureDesc& description)
{
    EXPECT_EQ(info.flags, 0);
    EXPECT_EQ(info.imageType, VK_IMAGE_TYPE_2D);
    EXPECT_EQ(info.format, static_cast<VkFormat>(description.format));
}

void verifyImageDimensions(const VkImageCreateInfo& info, const panda::TextureDesc& description)
{
    EXPECT_EQ(info.extent.width, description.extent.width);
    EXPECT_EQ(info.extent.height, description.extent.height);
    EXPECT_EQ(info.extent.depth, 1U);
    EXPECT_EQ(info.mipLevels, 1U);
    EXPECT_EQ(info.arrayLayers, 1U);
    EXPECT_EQ(info.samples, VK_SAMPLE_COUNT_1_BIT);
}

void verifyImageUsageAndLayout(const VkImageCreateInfo& info, const panda::TextureDesc& description)
{
    EXPECT_EQ(info.tiling, VK_IMAGE_TILING_OPTIMAL);
    EXPECT_EQ(info.usage, static_cast<VkImageUsageFlags>(description.usage));
    EXPECT_EQ(info.sharingMode, VK_SHARING_MODE_EXCLUSIVE);
    EXPECT_EQ(info.initialLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(info.pNext, nullptr);
}

void verifyDedicatedImageAllocation(const VkMemoryAllocateInfo& info, VkImage image)
{
    const auto* resource = static_cast<const VkMemoryDedicatedAllocateInfo*>(info.pNext);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->image, image);
    EXPECT_EQ(resource->buffer, VK_NULL_HANDLE);
}

// Explicit Action types avoid gMock's OnceAction ownership adapter, which
// triggers a spurious leak diagnostic in clang-tidy with MSVC headers.
class ContextResourcesIntegration : public testing::Test
{
protected:
    void expectInstance()
    {
        EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
            .WillOnce(testing::Action<decltype(vkEnumerateInstanceVersion)> {[](std::uint32_t* version) {
                *version = vk::ApiVersion13;
                return success;
            }});
        EXPECT_CALL(vulkan, vkCreateInstance(NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateInstance)> {
                [this](const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* output) {
                    EXPECT_EQ(info->pApplicationInfo->apiVersion, vk::ApiVersion13);
                    EXPECT_EQ(info->enabledLayerCount, 0U);
                    *output = instance;
                    return success;
                }});
    }

    void expectAdapter(bool windowed, bool separateQueues = false)
    {
        expectEnumeration();
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFeatures2(physical, NotNull()))
            .WillOnce(testing::Action<decltype(vkGetPhysicalDeviceFeatures2)> {
                [windowed](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
                    auto* const vulkan12 = static_cast<VkPhysicalDeviceVulkan12Features*>(features->pNext);
                    auto* const vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
                    vulkan12->timelineSemaphore = vk::True;
                    vulkan13->dynamicRendering = vk::True;
                    vulkan13->synchronization2 = vk::True;
                    if (windowed)
                    {
                        auto* const maintenance =
                            static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
                        maintenance->swapchainMaintenance1 = vk::True;
                    }
                }});
        expectQueueFamilies(separateQueues);
        if (windowed)
        {
            expectPresentationSupport(separateQueues);
        }
    }

    void expectEnumeration()
    {
        EXPECT_CALL(vulkan, vkEnumeratePhysicalDevices(instance, NotNull(), _))
            .Times(2)
            .WillRepeatedly([this](VkInstance, std::uint32_t* count, VkPhysicalDevice* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = physical;
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(physical, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* output) {
                *output = VkPhysicalDeviceProperties {};
                output->apiVersion = vk::ApiVersion13;
                output->deviceType = static_cast<VkPhysicalDeviceType>(vk::PhysicalDeviceType::eDiscreteGpu);
                std::ranges::copy(std::string_view {"Unit GPU"}, std::span {output->deviceName}.begin());
            });
    }

    void expectQueueFamilies(bool separate)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceQueueFamilyProperties(physical, NotNull(), _))
            .Times(2)
            .WillRepeatedly([separate](VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* output) {
                return populateQueueFamilies(separate, count, output);
            });
    }

    void expectPresentationSupport(bool separate)
    {
        EXPECT_CALL(vulkan, vkEnumerateDeviceExtensionProperties(physical, nullptr, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, const char*, std::uint32_t* count, VkExtensionProperties* output) {
                const auto entries = std::array {extension(vk::KHRSwapchainExtensionName),
                                                 extension(vk::KHRSwapchainMaintenance1ExtensionName)};
                *count = static_cast<std::uint32_t>(entries.size());
                if (output != nullptr)
                {
                    std::ranges::copy(entries, output);
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceSupportKHR(physical, 0, surface, NotNull()))
            .WillOnce(testing::Action<decltype(vkGetPhysicalDeviceSurfaceSupportKHR)> {
                [separate](VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32* supported) {
                    *supported = separate ? vk::False : vk::True;
                    return success;
                }});
        if (separate)
        {
            EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceSupportKHR(physical, 1, surface, NotNull()))
                .WillOnce(testing::Action<decltype(vkGetPhysicalDeviceSurfaceSupportKHR)> {
                    [](VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32* supported) {
                        *supported = vk::True;
                        return success;
                    }});
        }
        expectSurfaceFormats();
    }

    void expectSurfaceFormats()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkSurfaceFormatKHR* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = VkSurfaceFormatKHR {
                        .format = static_cast<VkFormat>(vk::Format::eB8G8R8A8Srgb),
                        .colorSpace = static_cast<VkColorSpaceKHR>(vk::ColorSpaceKHR::eSrgbNonlinear)};
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkPresentModeKHR* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = static_cast<VkPresentModeKHR>(vk::PresentModeKHR::eFifo);
                }
                return success;
            });
    }

    void expectWindow(panda::test::WindowSurfaceMock::SurfaceResult result)
    {
        EXPECT_CALL(window, getRequiredInstanceExtensions())
            .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtensionResult()> {
                Return(panda::test::WindowSurfaceMock::ExtensionResult {
                    std::vector<std::string> {vk::KHRSurfaceExtensionName}})});
        EXPECT_CALL(vulkan, vkEnumerateInstanceExtensionProperties(nullptr, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](const char*, std::uint32_t* count, VkExtensionProperties* output) {
                const auto entries = std::array {extension(vk::KHRSurfaceExtensionName),
                                                 extension(vk::KHRGetSurfaceCapabilities2ExtensionName),
                                                 extension(vk::KHRSurfaceMaintenance1ExtensionName)};
                *count = static_cast<std::uint32_t>(entries.size());
                if (output != nullptr)
                {
                    std::ranges::copy(entries, output);
                }
                return success;
            });
        EXPECT_CALL(window, createSurface(vk::Instance {instance}))
            .WillOnce(
                testing::Action<panda::test::WindowSurfaceMock::SurfaceResult(VkInstance)> {Return(std::move(result))});
    }

    void expectDevice(bool windowed, VkResult result = success, bool separateQueues = false)
    {
        if (result == success)
        {
            expectMemoryPool();
        }
        EXPECT_CALL(vulkan, vkCreateDevice(physical, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateDevice)> {
                [this, windowed, result, separateQueues](VkPhysicalDevice,
                                                         const VkDeviceCreateInfo* info,
                                                         const VkAllocationCallbacks*,
                                                         VkDevice* output) {
                    verifyDeviceFeatures(*info, windowed);
                    EXPECT_EQ(info->queueCreateInfoCount, separateQueues ? 2U : 1U);
                    EXPECT_EQ(info->enabledExtensionCount, windowed ? 2U : 0U);
                    if (windowed)
                    {
                        const auto names = std::span {info->ppEnabledExtensionNames, info->enabledExtensionCount};
                        EXPECT_EQ(std::string_view {names.front()}, vk::KHRSwapchainExtensionName);
                        EXPECT_EQ(std::string_view {names.back()}, vk::KHRSwapchainMaintenance1ExtensionName);
                    }
                    *output = device;
                    return result;
                }});
    }

    void expectParentsDestroyed(bool hasDevice, bool hasSurface)
    {
        auto parents = testing::ExpectationSet {};
        if (hasDevice)
        {
            parents += EXPECT_CALL(vulkan, vkDestroyDevice(device, nullptr))
                           .After(frameCleanup)
                           .After(memoryCleanup)
                           .After(imageCleanup);
        }
        if (hasSurface)
        {
            parents += EXPECT_CALL(vulkan, vkDestroySurfaceKHR(instance, surface, nullptr));
        }
        EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr)).After(parents);
    }

    void expectFrameResources()
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(device, _, 0, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkDevice, std::uint32_t, std::uint32_t, VkQueue* output) {
                *output = fakeVulkanHandle<VkQueue>(5);
            });
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(10)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(11)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(12)), Return(success))});
        EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkCommandPool>(20)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkCommandPool>(21)), Return(success))});
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(device, NotNull(), NotNull()))
            .WillOnce(testing::Action<decltype(vkAllocateCommandBuffers)> {
                testing::DoAll(testing::SetArgPointee<2>(fakeVulkanHandle<VkCommandBuffer>(30)), Return(success))})
            .WillOnce(testing::Action<decltype(vkAllocateCommandBuffers)> {
                testing::DoAll(testing::SetArgPointee<2>(fakeVulkanHandle<VkCommandBuffer>(31)), Return(success))});
    }

    void expectFramesDestroyed()
    {
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(12), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeVulkanHandle<VkCommandPool>(21), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(11), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeVulkanHandle<VkCommandPool>(20), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(10), nullptr));
    }

    void expectWindowed(bool separateQueues = false)
    {
        expectInstance();
        expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {surface});
        expectAdapter(true, separateQueues);
        expectDevice(true, success, separateQueues);
        expectFrameResources();
        expectFramesDestroyed();
        expectParentsDestroyed(true, true);
    }

    void expectMemoryPool()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(physical, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                *output = VkPhysicalDeviceMemoryProperties {};
                output->memoryTypeCount = 3;
                output->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                output->memoryTypes[1].propertyFlags = static_cast<VkMemoryPropertyFlags>(
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                output->memoryTypes[2].propertyFlags = static_cast<VkMemoryPropertyFlags>(
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
                output->memoryHeapCount = 1;
                output->memoryHeaps[0].size = std::uint64_t {128} * 1024 * 1024;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(physical, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                const auto* base = static_cast<const VkBaseOutStructure*>(output->pNext);
                if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES)
                {
                    auto* timeline = static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(output->pNext);
                    timeline->maxTimelineSemaphoreValueDifference = std::numeric_limits<std::uint64_t>::max();
                    return;
                }
                output->properties.limits.maxMemoryAllocationCount = 16;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = std::uint64_t {128} * 1024 * 1024;
            });
    }

    void expectUploadClock(vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(device, 0, 0, NotNull()))
            .WillOnce(testing::SetArgPointee<3>(fakeVulkanHandle<VkQueue>(5)));
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce(
                [result](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
                    *output = fakeVulkanHandle<VkSemaphore>(10);
                    return static_cast<VkResult>(result);
                });
        if (result == vk::Result::eSuccess)
        {
            frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(10), nullptr));
        }
    }

    void expectHeadless()
    {
        expectInstance();
        expectAdapter(false);
        expectDevice(false);
    }

    void expectBuffer(VkBuffer buffer,
                      std::uint64_t size = 65,
                      bool dedicated = false,
                      const testing::ExpectationSet& ownerReleased = {})
    {
        EXPECT_CALL(vulkan, vkCreateBuffer(device, NotNull(), nullptr, NotNull()))
            .InSequence(bufferCreationOrder)
            .WillOnce([buffer,
                       size](VkDevice, const VkBufferCreateInfo* info, const VkAllocationCallbacks*, VkBuffer* output) {
                EXPECT_EQ(info->size, size);
                EXPECT_EQ(info->usage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                EXPECT_EQ(info->sharingMode, VK_SHARING_MODE_EXCLUSIVE);
                EXPECT_EQ(info->flags, 0);
                EXPECT_EQ(info->pNext, nullptr);
                *output = buffer;
                return success;
            });
        EXPECT_CALL(vulkan, vkGetBufferMemoryRequirements2(device, NotNull(), NotNull()))
            .InSequence(bufferCreationOrder)
            .WillOnce([buffer, dedicated](VkDevice,
                                          const VkBufferMemoryRequirementsInfo2* info,
                                          VkMemoryRequirements2* output) {
                EXPECT_EQ(info->buffer, buffer);
                output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 7};
                auto* requirement = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(requirement, nullptr);
                requirement->requiresDedicatedAllocation = dedicated ? vk::True : vk::False;
            });
        bufferCleanup += EXPECT_CALL(vulkan, vkDestroyBuffer(device, buffer, nullptr)).After(ownerReleased);
    }

    void expectBufferAllocation(VkDeviceMemory memory, std::uint32_t type = 0, bool dedicated = false)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(device, NotNull(), nullptr, NotNull()))
            .InSequence(allocationOrder)
            .WillOnce([memory, type, dedicated](VkDevice,
                                                const VkMemoryAllocateInfo* info,
                                                const VkAllocationCallbacks*,
                                                VkDeviceMemory* output) {
                EXPECT_EQ(info->memoryTypeIndex, type);
                EXPECT_EQ(info->allocationSize, dedicated ? 128 : (type == 0 ? 64 : 8) * 1024 * 1024);
                EXPECT_EQ(info->pNext != nullptr, dedicated);
                *output = memory;
                return success;
            });
        if (type == 0)
        {
            memoryCleanup += EXPECT_CALL(vulkan, vkFreeMemory(device, memory, nullptr)).After(bufferCleanup);
        }
    }

    void expectBinding(VkBuffer buffer, VkDeviceMemory memory, std::uint64_t offset = 0, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkBindBufferMemory2(device, 1, NotNull()))
            .InSequence(bindingOrder)
            .WillOnce([buffer, memory, offset, result](VkDevice, std::uint32_t, const VkBindBufferMemoryInfo* info) {
                EXPECT_EQ(info->buffer, buffer);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, offset);
                return result;
            });
    }

    void expectImageCreation(VkImage image, const panda::TextureDesc& description)
    {
        EXPECT_CALL(vulkan, vkCreateImage(device, NotNull(), nullptr, NotNull()))
            .InSequence(imageCreationOrder)
            .WillOnce(
                [image,
                 description](VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* output) {
                    verifyImageIdentity(*info, description);
                    verifyImageDimensions(*info, description);
                    verifyImageUsageAndLayout(*info, description);
                    *output = image;
                    return success;
                });
    }

    void expectImageMemoryRequirements(VkImage image, bool dedicated)
    {
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(device, NotNull(), NotNull()))
            .InSequence(imageCreationOrder)
            .WillOnce([image,
                       dedicated](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                EXPECT_EQ(info->image, image);
                output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 7};
                auto* requirement = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(requirement, nullptr);
                requirement->requiresDedicatedAllocation = dedicated ? vk::True : vk::False;
            });
    }

    void expectTexture(VkImage image, const panda::TextureDesc& description, bool dedicated = false)
    {
        expectImageCreation(image, description);
        expectImageMemoryRequirements(image, dedicated);
    }

    void expectTextureCleanup(VkImage image, const testing::ExpectationSet& ownerReleased = {})
    {
        imageCleanup += EXPECT_CALL(vulkan, vkDestroyImage(device, image, nullptr)).After(ownerReleased);
    }

    void expectTextureViewCleanup(VkImage image, VkImageView view, const testing::ExpectationSet& ownerReleased = {})
    {
        const testing::Expectation viewReleased =
            EXPECT_CALL(vulkan, vkDestroyImageView(device, view, nullptr)).After(ownerReleased);
        imageCleanup += viewReleased;
        imageCleanup += EXPECT_CALL(vulkan, vkDestroyImage(device, image, nullptr)).After(viewReleased);
    }

    void expectTextureViewCreation(VkImage image, VkImageView view, const panda::TextureDesc& description)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(device, NotNull(), nullptr, NotNull()))
            .InSequence(imageBindingOrder)
            .WillOnce([image, view, format = description.format](VkDevice,
                                                                 const VkImageViewCreateInfo* info,
                                                                 const VkAllocationCallbacks*,
                                                                 VkImageView* output) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D);
                EXPECT_EQ(info->format, static_cast<VkFormat>(format));
                EXPECT_EQ(info->subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
                EXPECT_EQ(info->subresourceRange.levelCount, 1U);
                EXPECT_EQ(info->subresourceRange.layerCount, 1U);
                *output = view;
                return success;
            });
    }

    void expectTextureAllocation(VkDeviceMemory memory, VkImage image, std::uint32_t type = 0, bool dedicated = false)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(device, NotNull(), nullptr, NotNull()))
            .InSequence(imageAllocationOrder)
            .WillOnce([memory, image, type, dedicated](VkDevice,
                                                       const VkMemoryAllocateInfo* info,
                                                       const VkAllocationCallbacks*,
                                                       VkDeviceMemory* output) {
                EXPECT_EQ(info->memoryTypeIndex, type);
                EXPECT_EQ(info->allocationSize, dedicated ? 128 : (type == 0 ? 64 : 8) * 1024 * 1024);
                EXPECT_EQ(info->pNext != nullptr, dedicated);
                if (dedicated)
                {
                    verifyDedicatedImageAllocation(*info, image);
                }
                *output = memory;
                return success;
            });
        if (type == 0)
        {
            memoryCleanup +=
                EXPECT_CALL(vulkan, vkFreeMemory(device, memory, nullptr)).After(bufferCleanup).After(imageCleanup);
        }
    }

    void expectTextureAllocationFailure(VkDeviceMemory output, VkImage image)
    {
        EXPECT_CALL(vulkan, vkAllocateMemory(device, NotNull(), nullptr, NotNull()))
            .InSequence(imageAllocationOrder)
            .WillOnce([output, image](VkDevice,
                                      const VkMemoryAllocateInfo* info,
                                      const VkAllocationCallbacks*,
                                      VkDeviceMemory* memory) {
                EXPECT_EQ(info->allocationSize, 128U);
                verifyDedicatedImageAllocation(*info, image);
                *memory = output;
                return exhausted;
            });
        EXPECT_CALL(vulkan, vkFreeMemory(device, output, nullptr)).Times(0);
    }

    void expectTextureBinding(VkImage image, VkDeviceMemory memory, std::uint64_t offset = 0, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkBindImageMemory2(device, 1, NotNull()))
            .InSequence(imageBindingOrder)
            .WillOnce([image, memory, offset, result](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, offset);
                return result;
            });
    }

    void expectHostMapping(VkDeviceMemory memory)
    {
        EXPECT_CALL(vulkan, vkMapMemory(device, memory, 0, std::uint64_t {8} * 1024 * 1024, 0, NotNull()))
            .WillOnce([this](VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** output) {
                *output = hostStorage.data();
                return success;
            });
        const auto unmapped =
            testing::Expectation {EXPECT_CALL(vulkan, vkUnmapMemory(device, memory)).After(bufferCleanup)};
        memoryCleanup += EXPECT_CALL(vulkan, vkFreeMemory(device, memory, nullptr)).After(unmapped);
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::SwapchainGenerationMock> generations;
    testing::StrictMock<panda::test::WindowSurfaceMock> window;
    VkInstance instance {fakeVulkanHandle<VkInstance>(1)};
    VkPhysicalDevice physical {fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice device {fakeVulkanHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeVulkanHandle<VkSurfaceKHR>(4)};
    testing::ExpectationSet frameCleanup;
    testing::ExpectationSet bufferCleanup;
    testing::ExpectationSet imageCleanup;
    testing::ExpectationSet memoryCleanup;
    std::array<std::byte, 128> hostStorage {};
    testing::Sequence bufferCreationOrder;
    testing::Sequence bindingOrder;
    testing::Sequence allocationOrder;
    testing::Sequence imageCreationOrder;
    testing::Sequence imageBindingOrder;
    testing::Sequence imageAllocationOrder;
};
}

TEST_F(ContextResourcesIntegration, CreatesMovesAndDestroysHeadlessDeviceInDependencyOrder)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);
    auto context = panda::Context::create();
    ASSERT_TRUE(context.has_value());
    auto moved = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_EQ(moved.getDeviceInfo().name, "Unit GPU");
    EXPECT_EQ(moved.getDeviceInfo().queueFamily, 0U);
    const auto frame = moved.presentClearFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextResourcesIntegration, EmptyBufferAndInvalidDescriptionsNeverCreateNativeResources)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto empty = panda::Buffer {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.getSize(), 0);
    empty.reset();
    for (const auto description : std::array {
             panda::BufferDesc {},
             panda::BufferDesc {.size = 1, .usage = {}},
             panda::BufferDesc {.usage = vk::BufferUsageFlagBits::eStorageBuffer},
             panda::BufferDesc {.size = 1,
                                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                .memory = invalidMemoryIntent()}
    })
    {
        const auto buffer = context->createBuffer(description);
        ASSERT_FALSE(buffer);
        EXPECT_EQ(buffer.error().code, panda::ErrorCode::InvalidArgument);
    }
    const auto unsupported = context->createBuffer({.size = 1, .usage = vk::BufferUsageFlagBits::eShaderDeviceAddress});
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextResourcesIntegration, EmptyAndInvalidTexturesNeverCreateNativeImages)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto empty = panda::Texture {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.getExtent().width, 0U);
    EXPECT_EQ(empty.getExtent().height, 0U);
    EXPECT_EQ(empty.getFormat(), vk::Format::eUndefined);
    EXPECT_FALSE(empty.getUsage());
    empty.reset();
    const auto validUsage = vk::ImageUsageFlagBits::eSampled;
    const auto descriptions = std::array {
        panda::TextureDesc {},
        panda::TextureDesc {.extent = vk::Extent2D {.width = 0, .height = 1},
                            .format = vk::Format::eR8G8B8A8Unorm,
                            .usage = validUsage},
        panda::TextureDesc {.extent = vk::Extent2D {.width = 1, .height = 0},
                            .format = vk::Format::eR8G8B8A8Unorm,
                            .usage = validUsage},
        panda::TextureDesc {.extent = vk::Extent2D {.width = 1, .height = 1}, .usage = validUsage},
        panda::TextureDesc {.extent = vk::Extent2D {.width = 1, .height = 1},
                            .format = vk::Format::eR8G8B8A8Unorm,
                            .usage = {}},
    };
    for (const auto& description : descriptions)
    {
        const auto texture = context->createTexture(description);
        ASSERT_FALSE(texture);
        EXPECT_EQ(texture.error().code, panda::ErrorCode::InvalidArgument);
    }
    const auto unsupported = context->createTexture({
        .extent = vk::Extent2D {.width = 1, .height = 1},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eTransientAttachment
    });
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextResourcesIntegration, EmptyAndInvalidMeshesNeverCreateNativeBuffers)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto empty = panda::Mesh {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.getVertexCount(), 0);
    EXPECT_EQ(empty.getVertexStride(), 0);
    EXPECT_EQ(empty.getVertexSize(), 0);
    EXPECT_EQ(empty.getIndexCount(), 0);
    EXPECT_FALSE(empty.getIndexType());
    EXPECT_EQ(empty.getIndexSize(), 0);
    empty.reset();
    const auto descriptions = std::array {
        panda::MeshDesc {},
        panda::MeshDesc {.vertexCount = 3, .vertexStride = 0, .indices = std::nullopt},
        panda::MeshDesc {.vertexCount = 3,
                         .vertexStride = 12,
                         .indices = panda::MeshIndexDesc {.count = 0, .type = panda::MeshIndexType::UInt16}}
    };
    for (const auto& description : descriptions)
    {
        const auto mesh = context->createMesh(description);
        ASSERT_FALSE(mesh);
        EXPECT_EQ(mesh.error().code, panda::ErrorCode::InvalidArgument);
    }
}

TEST_F(ContextResourcesIntegration, SharedTexturesBindAndReleaseTheOptimalImage)
{
    expectHeadless();
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 64, .height = 32},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
    };
    auto* const image = fakeVulkanHandle<VkImage>(40);
    auto* const view = fakeVulkanHandle<VkImageView>(41);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectTexture(image, description);
    expectTextureViewCleanup(image, view);
    expectTextureAllocation(memory, image);
    expectTextureBinding(image, memory);
    expectTextureViewCreation(image, view, description);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto texture = context->createTexture(description);
    ASSERT_TRUE(texture);
    EXPECT_EQ(texture->getExtent().width, description.extent.width);
    EXPECT_EQ(texture->getExtent().height, description.extent.height);
    EXPECT_EQ(texture->getFormat(), description.format);
    EXPECT_EQ(texture->getUsage(), description.usage);
    auto copy = *texture;
    texture->reset();
    EXPECT_TRUE(copy.isValid());
    EXPECT_EQ(copy.getExtent().width, description.extent.width);
    copy.reset();
}

TEST_F(ContextResourcesIntegration, FailedImageCreationDoesNotAdoptItsUndefinedOutput)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 16, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eSampled
    };
    auto* const output = fakeVulkanHandle<VkImage>(40);
    EXPECT_CALL(vulkan, vkCreateImage(device, NotNull(), nullptr, NotNull()))
        .WillOnce([output](VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* image) {
            *image = output;
            return exhausted;
        });
    EXPECT_CALL(vulkan, vkDestroyImage(device, output, nullptr)).Times(0);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto texture = context->createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, FailedTextureAllocationDestroysTheUnboundImage)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 16, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eSampled
    };
    auto* const image = fakeVulkanHandle<VkImage>(40);
    auto* const output = fakeVulkanHandle<VkDeviceMemory>(50);
    expectTexture(image, description, true);
    expectTextureCleanup(image);
    expectTextureAllocationFailure(output, image);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto texture = context->createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, DedicatedTextureBindingFailureDestroysImageBeforeItsAllocation)
{
    expectHeadless();
    const auto description = panda::TextureDesc {
        .extent = vk::Extent2D {.width = 16, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eSampled
    };
    auto* const image = fakeVulkanHandle<VkImage>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectTexture(image, description, true);
    expectTextureCleanup(image);
    expectTextureAllocation(memory, image, 0, true);
    expectTextureBinding(image, memory, 0, exhausted);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto texture = context->createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(texture.error().native.value_or(panda::NativeError {}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, SharedBuffersRetainNativeStateAndReuseReleasedRanges)
{
    expectHeadless();
    auto* const first = fakeVulkanHandle<VkBuffer>(40);
    auto* const second = fakeVulkanHandle<VkBuffer>(41);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory);
    expectBinding(second, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto buffer = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(buffer);
    auto copy = *buffer;
    buffer->reset();
    EXPECT_TRUE(copy.isValid());
    EXPECT_EQ(copy.getSize(), 65);
    copy.reset();
    auto replacement = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->getSize(), 65);
}

TEST_F(ContextResourcesIntegration, LiveBuffersRemainBoundAcrossContextAndHandleMoves)
{
    expectHeadless();
    auto* const native = fakeVulkanHandle<VkBuffer>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto created = panda::Context::create();
    ASSERT_TRUE(created);
    auto buffer = created->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(buffer);
    auto context = std::move(*created);
    auto moved = std::move(*buffer);
    EXPECT_FALSE(buffer->isValid());
    EXPECT_EQ(buffer->getSize(), 0);
    EXPECT_TRUE(context.isValid());
    EXPECT_EQ(moved.getSize(), 65);
    moved.reset();
}

TEST_F(ContextResourcesIntegration, FailedBufferCreationDoesNotAdoptUndefinedNativeOutput)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    EXPECT_CALL(vulkan, vkCreateBuffer(device, NotNull(), nullptr, NotNull()))
        .WillOnce([](VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer* output) {
            *output = fakeVulkanHandle<VkBuffer>(40);
            return exhausted;
        });
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto buffer = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_FALSE(buffer);
    EXPECT_EQ(buffer.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(buffer.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, FailedBindingReleasesTheRangeAndPreservesTheNativeError)
{
    expectHeadless();
    auto* const first = fakeVulkanHandle<VkBuffer>(40);
    auto* const second = fakeVulkanHandle<VkBuffer>(41);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory, 0, exhausted);
    expectBinding(second, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto failed = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
    const auto retry = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(retry);
}

TEST_F(ContextResourcesIntegration, RequiredDedicatedBufferIsDestroyedBeforeItsExactAllocation)
{
    expectHeadless();
    auto* const native = fakeVulkanHandle<VkBuffer>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(native, 65, true);
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto buffer = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(buffer);
    buffer->reset();
    EXPECT_FALSE(buffer->isValid());
}

TEST_F(ContextResourcesIntegration, LastSharedOwnerControlsNativeDestruction)
{
    expectHeadless();
    auto* const native = fakeVulkanHandle<VkBuffer>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    auto lastRelease = testing::MockFunction<void()> {};
    const auto ownerReleased = testing::Expectation {EXPECT_CALL(lastRelease, Call())};
    expectBuffer(native, 65, true, testing::ExpectationSet {ownerReleased});
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto resetComplete = testing::MockFunction<void()> {};
    EXPECT_CALL(resetComplete, Call()).After(memoryCleanup);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto original = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(original);
    auto retained = *original;
    original->reset();
    EXPECT_EQ(original->getSize(), 0);
    EXPECT_EQ(retained.getSize(), 65);
    lastRelease.Call();
    retained.reset();
    resetComplete.Call();
    EXPECT_FALSE(retained.isValid());
    EXPECT_EQ(retained.getSize(), 0);
}

TEST_F(ContextResourcesIntegration, DedicatedBindingFailureDestroysBufferBeforeFreeingItsAllocation)
{
    expectHeadless();
    auto* const native = fakeVulkanHandle<VkBuffer>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(native, 65, true);
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory, 0, exhausted);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto failed = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(failed.error().native);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, AllocationFailureDestroysTheUnboundBuffer)
{
    expectHeadless();
    expectBuffer(fakeVulkanHandle<VkBuffer>(40));
    EXPECT_CALL(vulkan, vkAllocateMemory(device, NotNull(), nullptr, NotNull()))
        .Times(2)
        .WillRepeatedly(Return(exhausted));
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto failed = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, CopyAndMoveAssignmentReleaseOnlyTheReplacedOwner)
{
    expectHeadless();
    auto* const first = fakeVulkanHandle<VkBuffer>(40);
    auto* const second = fakeVulkanHandle<VkBuffer>(41);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory);
    expectBinding(second, memory, 128);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto source = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    auto target = context->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(source);
    ASSERT_TRUE(target);
    *target = *source;
    source->reset();
    auto assigned = panda::Buffer {};
    assigned = std::move(*target);
    EXPECT_FALSE(target->isValid());
    EXPECT_TRUE(assigned.isValid());
    EXPECT_EQ(assigned.getSize(), 65);
}

TEST_F(ContextResourcesIntegration, HostMemoryIntentsSelectCoherenceForUploadsAndCachingForReadback)
{
    expectHeadless();
    auto* const upload = fakeVulkanHandle<VkBuffer>(40);
    auto* const readback = fakeVulkanHandle<VkBuffer>(41);
    auto* const uploadMemory = fakeVulkanHandle<VkDeviceMemory>(50);
    auto* const readbackMemory = fakeVulkanHandle<VkDeviceMemory>(51);
    expectBuffer(upload);
    expectBuffer(readback);
    expectBufferAllocation(uploadMemory, 1);
    expectBufferAllocation(readbackMemory, 2);
    expectHostMapping(uploadMemory);
    expectHostMapping(readbackMemory);
    expectBinding(upload, uploadMemory);
    expectBinding(readback, readbackMemory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto first = context->createBuffer(
        {.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = panda::BufferMemory::Upload});
    const auto second = context->createBuffer(
        {.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = panda::BufferMemory::Readback});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
}

using ContextResourcesIntegrationDeathTest = ContextResourcesIntegration;

TEST_F(ContextResourcesIntegrationDeathTest, ContextRejectsDestructionBeforeTheLastResourceOwner)
{
    expectHeadless();
    auto* const native = fakeVulkanHandle<VkBuffer>(40);
    auto* const memory = fakeVulkanHandle<VkDeviceMemory>(50);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto context = panda::Context::create().transform([](panda::Context owner) {
        return std::make_unique<panda::Context>(std::move(owner));
    });
    ASSERT_TRUE(context);
    auto buffer = (*context)->createBuffer({.size = 65, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    ASSERT_TRUE(buffer);
    EXPECT_DEATH(context->reset(), "Context destroyed with live Buffer or Texture owners");
    buffer->reset();
}

TEST_F(ContextResourcesIntegration, DeviceCreationFailurePreservesNativeErrorWithoutAdoptingTheOutput)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false, exhausted);
    expectParentsDestroyed(false, false);
    EXPECT_CALL(vulkan, vkDestroyDevice(_, _)).Times(0);
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(device, _)).Times(0);
    const auto context = panda::Context::create();
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(context.error().native.has_value());
    EXPECT_EQ(context.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, FailedSurfaceCreationReleasesInstanceAndPreservesFacadeError)
{
    expectInstance();
    expectWindow(std::unexpected {panda::makeError(panda::ErrorCode::SurfaceLost, "surface lost")});
    expectParentsDestroyed(false, false);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::SurfaceLost);
    EXPECT_EQ(context.error().message, "surface lost");
}

TEST_F(ContextResourcesIntegration, NullSurfaceIsRejectedBeforeDeviceSelection)
{
    expectInstance();
    expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {VkSurfaceKHR {}});
    expectParentsDestroyed(false, false);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(ContextResourcesIntegration, WindowedDeviceFailureReleasesSurfaceBeforeInstance)
{
    expectInstance();
    expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {surface});
    expectAdapter(true);
    expectDevice(true, exhausted);
    expectParentsDestroyed(false, true);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, MovedSuspendedContextRetainsItsDeviceUntilAllFrameResourcesAreDestroyed)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .Times(2)
        .WillRepeatedly(Return(panda::test::WindowSurfaceMock::ExtentResult {panda::FramebufferExtent {}}));
    auto context = panda::Context::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
    auto moved = std::move(*context);
    const auto frame = moved.presentClearFrame();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Suspended);
}

TEST_F(ContextResourcesIntegration, SeparatePresentFamilyProducesTwoDeviceQueueRequests)
{
    expectWindowed(true);
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {panda::FramebufferExtent {}})});
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->getDeviceInfo().queueFamily, 0U);
    EXPECT_EQ(context->getDeviceInfo().presentQueueFamily, 1U);
}

TEST_F(ContextResourcesIntegration, ExtentQueryFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {
                std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "extent failed")}})});
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "extent failed");
}

TEST_F(ContextResourcesIntegration, GenerationFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {
                                                                 panda::FramebufferExtent {.width = 640, .height = 480}}
             )
    });
    EXPECT_CALL(generations, createSwapchainGeneration(_))
        .WillOnce(testing::Action<decltype(panda::detail::createSwapchainGeneration)> {
            [](const panda::detail::SwapchainGenerationRequest&) -> panda::Result<panda::detail::SwapchainGeneration> {
                return std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "generation failed")};
            }});
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "generation failed");
}

TEST_F(ContextResourcesIntegration, EmptyAndMovedFromContextsRejectCompletionOperations)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto moved = std::move(*context);
    const auto empty = panda::CompletionPoint {};
    const auto noWork = moved.poll(empty);
    ASSERT_FALSE(noWork);
    EXPECT_EQ(noWork.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(moved.wait(empty, std::chrono::nanoseconds {0}));
    EXPECT_FALSE(context->poll(empty));
    EXPECT_FALSE(context->wait(empty, std::chrono::nanoseconds {0}));
}

TEST_F(ContextResourcesIntegration, SuspendedContextRejectsEmptyPointsAndNegativeTimeout)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {panda::FramebufferExtent {}}));
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_TRUE(context);
    const auto empty = panda::CompletionPoint {};
    const auto polled = context->poll(empty);
    ASSERT_FALSE(polled);
    EXPECT_EQ(polled.error().code, panda::ErrorCode::InvalidArgument);
    const auto waited = context->wait(empty, std::chrono::nanoseconds {0});
    ASSERT_FALSE(waited);
    EXPECT_EQ(waited.error().code, panda::ErrorCode::InvalidArgument);
    const auto negative = context->wait(empty, std::chrono::nanoseconds {-1});
    ASSERT_FALSE(negative);
    EXPECT_EQ(negative.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(ContextResourcesIntegration, BatchMovesAcrossContextAndEmptySubmitRemainsRetryable)
{
    expectHeadless();
    expectUploadClock();
    expectParentsDestroyed(true, false);
    auto context = panda::Context::create();
    ASSERT_TRUE(context);
    auto batch = context->createUploadBatch();
    ASSERT_TRUE(batch);
    auto movedContext = std::move(*context);
    auto movedBatch = std::move(*batch);
    EXPECT_TRUE(movedBatch.isValid());
    EXPECT_FALSE(batch->isValid());
    const auto empty = movedBatch.submit();
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_TRUE(movedBatch.isValid());
    auto replacement = movedContext.createUploadBatch();
    ASSERT_TRUE(replacement);
    *replacement = std::move(movedBatch);
    EXPECT_TRUE(replacement->isValid());
    // UploadBatch explicitly supports querying its empty moved-from state.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(movedBatch.isValid());
    EXPECT_FALSE(context->createUploadBatch());
}

TEST_F(ContextResourcesIntegration, FailedUploadClockCreationLeavesContextUsable)
{
    expectHeadless();
    expectUploadClock(vk::Result::eErrorOutOfDeviceMemory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Context::create();
    ASSERT_TRUE(context);
    const auto batch = context->createUploadBatch();
    ASSERT_FALSE(batch);
    EXPECT_EQ(batch.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_TRUE(context->isValid());
}

TEST_F(ContextResourcesIntegrationDeathTest, ContextRejectsAnUnsubmittedBatchEvenWithoutBuffers)
{
    expectHeadless();
    expectUploadClock();
    expectParentsDestroyed(true, false);
    auto created = panda::Context::create();
    ASSERT_TRUE(created);
    auto context = std::optional<panda::Context> {std::move(*created)};
    {
        const auto batch = context->createUploadBatch();
        ASSERT_TRUE(batch);
        EXPECT_DEATH(context.reset(), "Context destroyed with live upload batches");
    }
}

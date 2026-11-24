// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/ResourceManager.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <string>
#include <string_view>
#include <test/program/ShaderResources.hpp>
#include <utility>
#include <vector>

#include "FakeHandle.hpp"
#include "ResourceMappings.hpp"
#include "ScopedMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"
#include "presentation/SwapchainGeneration.hpp"
#include "resources/TextureResource.hpp"

namespace
{
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto shaderCode(std::string_view key, panda::ShaderStage stage) -> panda::ShaderCode
{
    const auto resource = panda::expect(test::program::findShader(key), "Shader program test fixture is missing");
    return {.words = resource.words, .stage = stage, .label = resource.key};
}

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

auto queueFamily(VkQueueFlags flags, std::uint32_t timestampValidBits = 0) -> VkQueueFamilyProperties
{
    return {
        .queueFlags = flags,
        .queueCount = 1,
        .timestampValidBits = timestampValidBits,
        .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
    };
}

auto populateQueueFamilies(bool separate,
                           std::uint32_t* count,
                           VkQueueFamilyProperties* output,
                           std::uint32_t timestampValidBits = 0) -> VkResult
{
    *count = separate ? 2U : 1U;
    if (output != nullptr)
    {
        auto families = std::span {output, *count};
        families.front() =
            queueFamily(static_cast<VkQueueFlags>(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute),
                        timestampValidBits);
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
    EXPECT_EQ(features->features.samplerAnisotropy, vk::True);
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

void verifyImageIdentity(const VkImageCreateInfo& info, const panda::detail::TextureResourceCreateInfo& description)
{
    EXPECT_EQ(info.flags, 0);
    EXPECT_EQ(info.imageType, VK_IMAGE_TYPE_2D);
    EXPECT_EQ(info.format, static_cast<VkFormat>(description.format));
}

void verifyImageDimensions(const VkImageCreateInfo& info, const panda::detail::TextureResourceCreateInfo& description)
{
    EXPECT_EQ(info.extent.width, description.extent.width);
    EXPECT_EQ(info.extent.height, description.extent.height);
    EXPECT_EQ(info.extent.depth, 1U);
    EXPECT_EQ(info.mipLevels, description.mipLevels);
    EXPECT_EQ(info.arrayLayers, 1U);
    EXPECT_EQ(info.samples, VK_SAMPLE_COUNT_1_BIT);
}

void verifyImageUsageAndLayout(const VkImageCreateInfo& info,
                               const panda::detail::TextureResourceCreateInfo& description)
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

    void expectAdapter(bool windowed, bool separateQueues = false, std::uint32_t timestampValidBits = 0)
    {
        expectEnumeration();
        expectRenderFormatSupport();
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
        expectQueueFamilies(separateQueues, timestampValidBits);
        if (windowed)
        {
            expectPresentationSupport(separateQueues);
        }
    }

    void expectRenderFormatSupport()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16B16A16_SFLOAT, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
                properties->optimalTilingFeatures =
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) |
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) |
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
            });
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceImageFormatProperties(physical, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice,
                               VkFormat,
                               VkImageType,
                               VkImageTiling,
                               VkImageUsageFlags,
                               VkImageCreateFlags,
                               VkImageFormatProperties* properties) {
                properties->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                properties->sampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                           static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                           static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);
                return success;
            });
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
                output->limits.maxStorageBufferRange = 4096;
                output->limits.maxDescriptorSetStorageBuffers = 4;
                output->limits.maxPerStageDescriptorStorageBuffers = 4;
                output->limits.timestampPeriod = 2.5F;
                std::ranges::copy(std::string_view {"Unit GPU"}, std::span {output->deviceName}.begin());
            });
    }

    void expectQueueFamilies(bool separate, std::uint32_t timestampValidBits = 0)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceQueueFamilyProperties(physical, NotNull(), _))
            .Times(2)
            .WillRepeatedly([separate, timestampValidBits](VkPhysicalDevice,
                                                           std::uint32_t* count,
                                                           VkQueueFamilyProperties* output) {
                return populateQueueFamilies(separate, count, output, timestampValidBits);
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
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceFeatures2(physical, testing::Truly([](const VkPhysicalDeviceFeatures2* info) {
                                                     return info->pNext == nullptr;
                                                 })))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
                features->features.samplerAnisotropy = vk::True;
            });
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceProperties2(physical, testing::Truly([](const VkPhysicalDeviceProperties2* output) {
                                               const auto* base = static_cast<const VkBaseOutStructure*>(output->pNext);
                                               return base != nullptr &&
                                                      base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
                                           })))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                auto* identity = static_cast<VkPhysicalDeviceIDProperties*>(output->pNext);
                identity->deviceUUID[0] = 1;
            });
        expectDriverProperties();
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
                *output = fakeHandle<VkQueue>(5);
            });
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkSemaphore>(10)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkSemaphore>(11)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateSemaphore)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkSemaphore>(12)), Return(success))});
        EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkCommandPool>(20)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkCommandPool>(21)), Return(success))});
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(device, NotNull(), NotNull()))
            .WillOnce(testing::Action<decltype(vkAllocateCommandBuffers)> {
                testing::DoAll(testing::SetArgPointee<2>(fakeHandle<VkCommandBuffer>(30)), Return(success))})
            .WillOnce(testing::Action<decltype(vkAllocateCommandBuffers)> {
                testing::DoAll(testing::SetArgPointee<2>(fakeHandle<VkCommandBuffer>(31)), Return(success))});
    }

    void expectFramesDestroyed()
    {
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeHandle<VkSemaphore>(12), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeHandle<VkCommandPool>(21), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeHandle<VkSemaphore>(11), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeHandle<VkCommandPool>(20), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeHandle<VkSemaphore>(10), nullptr));
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
                if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)
                {
                    auto* identity = static_cast<VkPhysicalDeviceIDProperties*>(output->pNext);
                    identity->deviceUUID[0] = 1;
                    return;
                }
                output->properties.limits.maxMemoryAllocationCount = 16;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = std::uint64_t {128} * 1024 * 1024;
            });
    }

    void expectDriverProperties()
    {
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceProperties2(physical, testing::Truly([](const VkPhysicalDeviceProperties2* output) {
                                               const auto* base = static_cast<const VkBaseOutStructure*>(output->pNext);
                                               return base != nullptr &&
                                                      base->sType ==
                                                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
                                           })))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                auto* driver = static_cast<VkPhysicalDeviceDriverProperties*>(output->pNext);
                std::ranges::copy(std::string_view {"Integration Vulkan driver"},
                                  std::span {driver->driverName}.begin());
                std::ranges::copy(std::string_view {"Integration test driver 1.0"},
                                  std::span {driver->driverInfo}.begin());
            });
    }

    void expectUploadClock(vk::Result result = vk::Result::eSuccess)
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(device, 0, 0, NotNull()))
            .WillOnce(testing::SetArgPointee<3>(fakeHandle<VkQueue>(5)));
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce(
                [result](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* output) {
                    *output = fakeHandle<VkSemaphore>(10);
                    return static_cast<VkResult>(result);
                });
        if (result == vk::Result::eSuccess)
        {
            frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeHandle<VkSemaphore>(10), nullptr));
        }
    }

    void expectPerformanceQueryPools(std::uint32_t timestampValidBits)
    {
        if (timestampValidBits == 0)
        {
            return;
        }
        const auto queryPoolCreationOrder = testing::Sequence {};
        for (auto index = std::uint64_t {}; index < 4; ++index)
        {
            auto* const queryPool = fakeHandle<VkQueryPool>(70 + index);
            EXPECT_CALL(vulkan, vkCreateQueryPool(device, NotNull(), nullptr, NotNull()))
                .InSequence(queryPoolCreationOrder)
                .WillOnce([queryPool](VkDevice,
                                      const VkQueryPoolCreateInfo* info,
                                      const VkAllocationCallbacks*,
                                      VkQueryPool* output) {
                    EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                    EXPECT_EQ(info->queryCount, 2U);
                    *output = queryPool;
                    return success;
                });
            frameCleanup += EXPECT_CALL(vulkan, vkDestroyQueryPool(device, queryPool, nullptr));
        }
    }

    void expectHeadless(vk::Result timelineResult = vk::Result::eSuccess, std::uint32_t timestampValidBits = 0)
    {
        expectInstance();
        expectAdapter(false, false, timestampValidBits);
        expectDevice(false);
        expectUploadClock(timelineResult);
        expectPerformanceQueryPools(timestampValidBits);
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(physical,
                                                             _,
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             _,
                                                             0,
                                                             NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice,
                               VkFormat,
                               VkImageType,
                               VkImageTiling,
                               VkImageUsageFlags,
                               VkImageCreateFlags,
                               VkImageFormatProperties* output) {
                *output = VkImageFormatProperties {};
                output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                output->sampleCounts = static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT) |
                                       static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_2_BIT) |
                                       static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_4_BIT);
                return success;
            });
    }

    void expectBuffer(VkBuffer buffer,
                      std::uint64_t size = 65,
                      bool dedicated = false,
                      const testing::ExpectationSet& ownerReleased = {},
                      VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    {
        EXPECT_CALL(vulkan, vkCreateBuffer(device, NotNull(), nullptr, NotNull()))
            .InSequence(bufferCreationOrder)
            .WillOnce([buffer, size, usage](VkDevice,
                                            const VkBufferCreateInfo* info,
                                            const VkAllocationCallbacks*,
                                            VkBuffer* output) {
                EXPECT_EQ(info->size, size);
                EXPECT_EQ(info->usage, usage);
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

    void expectGeometryMesh(VkBuffer vertices,
                            VkDeviceMemory memory,
                            std::uint64_t vertexSize,
                            std::optional<VkBuffer> indices = std::nullopt,
                            std::uint64_t indexSize = 0)
    {
        constexpr auto vertexUsage = static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferDst |
                                                                     vk::BufferUsageFlagBits::eVertexBuffer);
        constexpr auto indexUsage = static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferDst |
                                                                    vk::BufferUsageFlagBits::eIndexBuffer);
        expectBuffer(vertices, vertexSize, false, {}, vertexUsage);
        expectBufferAllocation(memory);
        expectBinding(vertices, memory);
        if (indices)
        {
            expectBuffer(*indices, indexSize, false, {}, indexUsage);
            expectBinding(*indices, memory, 128);
        }
        expectParentsDestroyed(true, false);
    }

    void expectImageCreation(VkImage image, const panda::detail::TextureResourceCreateInfo& description)
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(physical,
                                                             static_cast<VkFormat>(description.format),
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             static_cast<VkImageUsageFlags>(description.usage),
                                                             0,
                                                             NotNull()))
            .InSequence(imageCreationOrder)
            .WillOnce([](VkPhysicalDevice,
                         VkFormat,
                         VkImageType,
                         VkImageTiling,
                         VkImageUsageFlags,
                         VkImageCreateFlags,
                         VkImageFormatProperties* output) {
                *output = VkImageFormatProperties {};
                output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                return success;
            });
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

    void expectTexture(VkImage image,
                       const panda::detail::TextureResourceCreateInfo& description,
                       bool dedicated = false)
    {
        expectImageCreation(image, description);
        expectImageMemoryRequirements(image, dedicated);
    }

    void expectTexture(VkImage image, const panda::TextureDesc& description, bool dedicated = false)
    {
        const auto nativeDescription = panda::detail::TextureResourceCreateInfo {
            .extent = {.width = description.extent.x, .height = description.extent.y},
            .format = panda::expect(panda::detail::mapPixelFormat(description.format), "Map texture format"),
            .usage = panda::expect(panda::detail::mapTextureUsage(description.usage), "Map texture usage")
        };
        expectTexture(image, nativeDescription, dedicated);
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

    void expectTextureViewCreation(VkImage image,
                                   VkImageView view,
                                   const panda::detail::TextureResourceCreateInfo& description)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(device, NotNull(), nullptr, NotNull()))
            .InSequence(imageBindingOrder)
            .WillOnce([image, view, format = description.format, mipLevels = description.mipLevels](
                          VkDevice,
                          const VkImageViewCreateInfo* info,
                          const VkAllocationCallbacks*,
                          VkImageView* output) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D);
                EXPECT_EQ(info->format, static_cast<VkFormat>(format));
                EXPECT_EQ(info->subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
                EXPECT_EQ(info->subresourceRange.levelCount, mipLevels);
                EXPECT_EQ(info->subresourceRange.layerCount, 1U);
                *output = view;
                return success;
            });
    }

    void expectTextureViewCreation(VkImage image, VkImageView view, const panda::TextureDesc& description)
    {
        const auto nativeDescription = panda::detail::TextureResourceCreateInfo {
            .extent = {.width = description.extent.x, .height = description.extent.y},
            .format = panda::expect(panda::detail::mapPixelFormat(description.format), "Map texture format"),
            .usage = panda::expect(panda::detail::mapTextureUsage(description.usage), "Map texture usage"),
            .mipLevels =
                description.mipPolicy == panda::TextureMipPolicy::SingleLevel
                    ? 1U
                    : static_cast<std::uint32_t>(std::bit_width(std::max(description.extent.x, description.extent.y)))
        };
        expectTextureViewCreation(image, view, nativeDescription);
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
    VkInstance instance {fakeHandle<VkInstance>(1)};
    VkPhysicalDevice physical {fakeHandle<VkPhysicalDevice>(2)};
    VkDevice device {fakeHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeHandle<VkSurfaceKHR>(4)};
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
    expectHeadless(vk::Result::eSuccess, 64);
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create({.enableFramePerformanceSampling = true});
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->getTextureFilteringPreset(), panda::TextureFilteringPreset::Anisotropic8x);
    ASSERT_TRUE(context->setTextureFilteringPreset(panda::TextureFilteringPreset::Bilinear));
    EXPECT_EQ(context->getTextureFilteringPreset(), panda::TextureFilteringPreset::Bilinear);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalidPreset =
        context->setTextureFilteringPreset(static_cast<panda::TextureFilteringPreset>(std::uint8_t {255}));
    ASSERT_FALSE(invalidPreset);
    EXPECT_EQ(invalidPreset.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(context->getTextureFilteringPreset(), panda::TextureFilteringPreset::Bilinear);
    const auto* resourceManager = &context->getResourceManager();
    const auto* submissionManager = &context->getSubmissionManager();
    const auto* offscreenRenderer = &context->getOffscreenRenderer();
    const auto* performanceManager = &context->getPerformanceManager();
    const auto* externalBufferManager = &context->getExternalBufferManager();
    auto moved = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_EQ(&moved.getResourceManager(), resourceManager);
    EXPECT_EQ(&moved.getSubmissionManager(), submissionManager);
    EXPECT_EQ(&moved.getOffscreenRenderer(), offscreenRenderer);
    EXPECT_EQ(&moved.getPerformanceManager(), performanceManager);
    EXPECT_EQ(&moved.getExternalBufferManager(), externalBufferManager);
    EXPECT_EQ(moved.getDeviceInfo().name, "Unit GPU");
    EXPECT_EQ(moved.getDeviceInfo().driverName, "Integration Vulkan driver");
    EXPECT_EQ(moved.getDeviceInfo().driverInfo, "Integration test driver 1.0");
}

TEST_F(ContextResourcesIntegration, UsesTextureFilteringPresetFromEngineOptions)
{
    expectHeadless(vk::Result::eSuccess, 64);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create(
        {.enableFramePerformanceSampling = true, .textureFilteringPreset = panda::TextureFilteringPreset::Trilinear});
    ASSERT_TRUE(context);
    EXPECT_EQ(context->getTextureFilteringPreset(), panda::TextureFilteringPreset::Trilinear);
}

TEST_F(ContextResourcesIntegration, RejectsInvalidInitialTextureFilteringPreset)
{
    const auto created = panda::Engine::create(
        {.textureFilteringPreset =
             // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- Exercise validation of an invalid preset.
         static_cast<panda::TextureFilteringPreset>(std::uint8_t {255})});
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(ContextResourcesIntegration, EmptyBufferAndInvalidDescriptionsNeverCreateNativeResources)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto empty = panda::Buffer {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.getSize(), 0);
    empty.reset();
    for (const auto description : std::array {
             panda::BufferDesc {},
             panda::BufferDesc {.size = 1, .usage = {}},
             panda::BufferDesc {.usage = ::panda::BufferUsage::Storage},
             panda::BufferDesc {.size = 1, .usage = ::panda::BufferUsage::Storage, .memory = invalidMemoryIntent()}
    })
    {
        const auto buffer = context->getResourceManager().createBuffer(description);
        ASSERT_FALSE(buffer);
        EXPECT_EQ(buffer.error().code, panda::ErrorCode::InvalidArgument);
    }
    const auto unsupported = context->getResourceManager().createBuffer(
        {.size = 1, .usage = std::bit_cast<::panda::BufferUsage>(std::uint8_t {0x80})});
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextResourcesIntegration, CreatesProgramWithAnExplicitShadowUsingFewerBindings)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto engine = panda::Engine::create();
    ASSERT_TRUE(engine);
    const auto vertex = shaderCode("fixtures/shaders/program_camera.vert", panda::ShaderStage::Vertex);
    const auto temporaryWords = std::vector<std::uint32_t>(vertex.words.begin(), vertex.words.end());
    const auto program = engine->getResourceManager().createShaderProgram({
        .vertex = {.words = temporaryWords,                                       .stage = vertex.stage,       .label = vertex.label},
        .fragment = shaderCode("fixtures/shaders/program_camera.frag", panda::ShaderStage::Fragment),
        .vertexInput = panda::ShaderVertexInput::Procedural,
        .shadow = panda::ShaderShadowStages {
                   .vertex = shaderCode("fixtures/shaders/readonly_storage.vert", panda::ShaderStage::Vertex),
                   .fragment = std::nullopt                                                                                         }
    });

    ASSERT_TRUE(program) << program.error().message;
}

TEST_F(ContextResourcesIntegration, RejectsShadowAbiReloadAndKeepsThePublishedRevision)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto engine = panda::Engine::create();
    ASSERT_TRUE(engine);
    auto description = panda::ProgramDesc {
        .vertex = shaderCode("fixtures/shaders/readonly_storage.vert", panda::ShaderStage::Vertex),
        .fragment = shaderCode("fixtures/shaders/program_camera.frag", panda::ShaderStage::Fragment),
        .vertexInput = panda::ShaderVertexInput::Procedural,
        .shadow = panda::ShaderShadowStages {
                                             .vertex = shaderCode("fixtures/shaders/readonly_storage.vert", panda::ShaderStage::Vertex),
                                             .fragment = std::nullopt}
    };
    const auto program = engine->getResourceManager().createShaderProgram(description);
    ASSERT_TRUE(program) << program.error().message;

    description.shadow = panda::ShaderShadowStages {
        .vertex = shaderCode("fixtures/shaders/program_shadow_empty.vert", panda::ShaderStage::Vertex),
        .fragment = std::nullopt};
    const auto rejected = engine->getResourceManager().reloadShaderProgram(*program, description);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, panda::ErrorCode::InvalidArgument);

    description.shadow = panda::ShaderShadowStages {
        .vertex = shaderCode("fixtures/shaders/readonly_storage.vert", panda::ShaderStage::Vertex),
        .fragment = std::nullopt};
    const auto retained = engine->getResourceManager().reloadShaderProgram(*program, description);
    ASSERT_TRUE(retained) << retained.error().message;
}

TEST_F(ContextResourcesIntegration, RejectsShadowBindingsWithDifferentLayoutsOrNoCameraBinding)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto engine = panda::Engine::create();
    ASSERT_TRUE(engine);
    const auto camera = shaderCode("fixtures/shaders/readonly_storage.vert", panda::ShaderStage::Vertex);
    const auto fragment = shaderCode("fixtures/shaders/program_camera.frag", panda::ShaderStage::Fragment);
    for (const auto* key :
         std::array {"fixtures/shaders/program_wrong_shadow.vert", "fixtures/shaders/program_camera.vert"})
    {
        SCOPED_TRACE(key);
        const auto program = engine->getResourceManager().createShaderProgram({
            .vertex = camera,
            .fragment = fragment,
            .vertexInput = panda::ShaderVertexInput::Procedural,
            .shadow = panda::ShaderShadowStages {.vertex = shaderCode(key, panda::ShaderStage::Vertex),
                                                 .fragment = std::nullopt}
        });
        ASSERT_FALSE(program);
        EXPECT_NE(program.error().message.find("has no matching camera layout"), std::string::npos);
    }
}

TEST_F(ContextResourcesIntegration, EmptyAndInvalidTexturesNeverCreateNativeImages)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto empty = panda::Texture {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.getExtent().x, 0U);
    EXPECT_EQ(empty.getExtent().y, 0U);
    EXPECT_EQ(empty.getFormat(), ::panda::PixelFormat::Undefined);
    EXPECT_EQ(empty.getUsage(), panda::TextureUsage::None);
    empty.reset();
    const auto validUsage = ::panda::TextureUsage::Sampled;
    const auto descriptions = std::array {
        panda::TextureDesc {},
        panda::TextureDesc {.extent = glm::uvec2 {0, 1},
                            .format = ::panda::PixelFormat::Rgba8Unorm,
                            .usage = validUsage},
        panda::TextureDesc {.extent = glm::uvec2 {1, 0},
                            .format = ::panda::PixelFormat::Rgba8Unorm,
                            .usage = validUsage},
        panda::TextureDesc {.extent = glm::uvec2 {1, 1}, .usage = validUsage},
        panda::TextureDesc {.extent = glm::uvec2 {1, 1}, .format = ::panda::PixelFormat::Rgba8Unorm, .usage = {}},
    };
    for (const auto& description : descriptions)
    {
        const auto texture = context->getResourceManager().createTexture(description);
        ASSERT_FALSE(texture);
        EXPECT_EQ(texture.error().code, panda::ErrorCode::InvalidArgument);
    }
    const auto unsupported = context->getResourceManager().createTexture({
        .extent = glm::uvec2 {1, 1},
        .format = ::panda::PixelFormat::Rgba8Unorm,
        .usage = std::bit_cast<::panda::TextureUsage>(std::uint8_t {0x80}
           )
    });
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextResourcesIntegration, EmptyAndInvalidMeshesNeverCreateNativeBuffers)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
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
        panda::MeshDesc {.vertexCount = 3, .vertexStride = 0, .indices = std::nullopt, .attributes = {}},
        panda::MeshDesc {.vertexCount = 3,
                         .vertexStride = 12,
                         .indices = panda::MeshIndexDesc {.count = 0, .type = panda::MeshIndexType::UInt16},
                         .attributes = {}}
    };
    for (const auto& description : descriptions)
    {
        const auto mesh = context->getResourceManager().createMesh(description);
        ASSERT_FALSE(mesh);
        EXPECT_EQ(mesh.error().code, panda::ErrorCode::InvalidArgument);
    }
}

TEST_F(ContextResourcesIntegration, CreatesIndexedAndNonIndexedMeshesWithSharedStorage)
{
    expectHeadless();
    constexpr auto vertexBufferUsage =
        static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer);
    constexpr auto indexBufferUsage =
        static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer);
    auto* const indexedVertices = fakeHandle<VkBuffer>(40);
    auto* const indices = fakeHandle<VkBuffer>(41);
    auto* const nonIndexedVertices = fakeHandle<VkBuffer>(42);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(indexedVertices, 36, false, {}, vertexBufferUsage);
    expectBuffer(indices, 6, false, {}, indexBufferUsage);
    expectBuffer(nonIndexedVertices, 32, false, {}, vertexBufferUsage);
    expectBufferAllocation(memory);
    expectBinding(indexedVertices, memory);
    expectBinding(indices, memory, 128);
    expectBinding(nonIndexedVertices, memory, 256);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);

    auto indexedResult = context->getResourceManager().createMesh({
        .vertexCount = 3,
        .vertexStride = 12,
        .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16},
        .attributes = {}
    });
    ASSERT_TRUE(indexedResult);
    auto indexed = *indexedResult;
    indexedResult->reset();
    EXPECT_TRUE(indexed.isValid());
    EXPECT_EQ(indexed.getVertexCount(), 3);
    EXPECT_EQ(indexed.getVertexStride(), 12);
    EXPECT_EQ(indexed.getVertexSize(), 36);
    EXPECT_EQ(indexed.getIndexCount(), 3);
    EXPECT_EQ(indexed.getIndexType(), panda::MeshIndexType::UInt16);
    EXPECT_EQ(indexed.getIndexSize(), 6);

    auto nonIndexedResult = context->getResourceManager().createMesh(
        {.vertexCount = 4, .vertexStride = 8, .indices = std::nullopt, .attributes = {}});
    ASSERT_TRUE(nonIndexedResult);
    auto nonIndexed = std::move(*nonIndexedResult);
    EXPECT_EQ(nonIndexed.getVertexSize(), 32);
    EXPECT_EQ(nonIndexed.getIndexCount(), 0);
    EXPECT_FALSE(nonIndexed.getIndexType());
    EXPECT_EQ(nonIndexed.getIndexSize(), 0);
    indexed.reset();
    nonIndexed.reset();
}

TEST_F(ContextResourcesIntegration, CreatesAndValidatesSixteenBitStandardGeometry)
{
    expectHeadless();
    auto* const vertices = fakeHandle<VkBuffer>(40);
    auto* const indices = fakeHandle<VkBuffer>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectGeometryMesh(vertices, memory, 192, indices, 6);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto geometry = panda::Geometry {
        .vertices = std::vector<panda::MeshVertex>(3),
        .indices = std::vector<std::uint16_t> {0, 1, 2}
    };
    const auto mesh = context->getResourceManager().createMeshFromGeometry(geometry);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->getVertexStride(), panda::standardMeshVertexStrideBytes);
    EXPECT_EQ(mesh->getIndexType(), panda::MeshIndexType::UInt16);

    auto batch = panda::UploadBatch {};
    EXPECT_FALSE(batch.write(panda::Mesh {}, geometry).has_value());
    auto mismatched = geometry;
    mismatched.indices = std::vector<std::uint32_t> {0, 1, 2};
    EXPECT_FALSE(batch.write(*mesh, mismatched).has_value());
    auto outOfRange = geometry;
    outOfRange.indices = std::vector<std::uint16_t> {0, 1, 3};
    EXPECT_FALSE(batch.write(*mesh, outOfRange).has_value());
    auto nonIndexed = geometry;
    nonIndexed.indices = std::vector<std::uint16_t> {};
    EXPECT_FALSE(batch.write(*mesh, nonIndexed).has_value());
    EXPECT_FALSE(batch.write(*mesh, geometry).has_value());
}

TEST_F(ContextResourcesIntegration, CreatesThirtyTwoBitStandardGeometryMesh)
{
    expectHeadless();
    auto* const vertices = fakeHandle<VkBuffer>(40);
    auto* const indices = fakeHandle<VkBuffer>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectGeometryMesh(vertices, memory, 192, indices, 12);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto geometry = panda::Geometry {
        .vertices = std::vector<panda::MeshVertex>(3),
        .indices = std::vector<std::uint32_t> {0, 1, 2}
    };
    const auto mesh = context->getResourceManager().createMeshFromGeometry(geometry);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->getIndexType(), panda::MeshIndexType::UInt32);
    EXPECT_EQ(mesh->getIndexSize(), 12);
}

TEST_F(ContextResourcesIntegration, CreatesNonIndexedStandardGeometryMesh)
{
    expectHeadless();
    auto* const vertices = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectGeometryMesh(vertices, memory, 64);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto geometry =
        panda::Geometry {.vertices = std::vector<panda::MeshVertex>(1), .indices = std::vector<std::uint16_t> {}};
    const auto mesh = context->getResourceManager().createMeshFromGeometry(geometry);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->getVertexSize(), panda::standardMeshVertexStrideBytes);
    EXPECT_EQ(mesh->getIndexCount(), 0);
    EXPECT_FALSE(mesh->getIndexType());

    auto indexed = geometry;
    indexed.indices = std::vector<std::uint16_t> {0};
    auto batch = panda::UploadBatch {};
    const auto write = batch.write(*mesh, indexed);
    ASSERT_FALSE(write);
    EXPECT_EQ(write.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(ContextResourcesIntegration, ActiveBatchRejectsAnEmptyMeshAndRemainsUsable)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto batch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(batch);
    const auto mesh = panda::Mesh {};
    const auto written = batch->write(mesh, {}, {});

    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_TRUE(batch->isValid());
}

TEST_F(ContextResourcesIntegration, SharedTexturesBindAndReleaseTheOptimalImage)
{
    expectHeadless();
    const auto description = panda::TextureDesc {
        .extent = glm::uvec2 {64, 32},
        .format = ::panda::PixelFormat::Rgba8Unorm,
        .usage = ::panda::TextureUsage::Sampled | ::panda::TextureUsage::TransferDestination
    };
    auto* const image = fakeHandle<VkImage>(40);
    auto* const view = fakeHandle<VkImageView>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectTexture(image, description);
    expectTextureViewCleanup(image, view);
    expectTextureAllocation(memory, image);
    expectTextureBinding(image, memory);
    expectTextureViewCreation(image, view, description);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto texture = context->getResourceManager().createTexture(description);
    ASSERT_TRUE(texture);
    EXPECT_EQ(texture->getExtent().x, description.extent.x);
    EXPECT_EQ(texture->getExtent().y, description.extent.y);
    EXPECT_EQ(texture->getFormat(), description.format);
    EXPECT_EQ(texture->getUsage(), description.usage);
    auto copy = *texture;
    texture->reset();
    EXPECT_TRUE(copy.isValid());
    EXPECT_EQ(copy.getExtent().x, description.extent.x);
    copy.reset();
}

TEST_F(ContextResourcesIntegration, FailedImageCreationDoesNotAdoptItsUndefinedOutput)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto description = panda::TextureDesc {
        .extent = glm::uvec2 {16, 16},
        .format = ::panda::PixelFormat::Rgba8Unorm,
        .usage = ::panda::TextureUsage::Sampled
    };
    auto* const output = fakeHandle<VkImage>(40);
    EXPECT_CALL(vulkan, vkCreateImage(device, NotNull(), nullptr, NotNull()))
        .WillOnce([output](VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* image) {
            *image = output;
            return exhausted;
        });
    EXPECT_CALL(vulkan, vkDestroyImage(device, output, nullptr)).Times(0);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto texture = context->getResourceManager().createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, FailedTextureAllocationDestroysTheUnboundImage)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    const auto description = panda::TextureDesc {
        .extent = glm::uvec2 {16, 16},
        .format = ::panda::PixelFormat::Rgba8Unorm,
        .usage = ::panda::TextureUsage::Sampled
    };
    auto* const image = fakeHandle<VkImage>(40);
    auto* const output = fakeHandle<VkDeviceMemory>(50);
    expectTexture(image, description, true);
    expectTextureCleanup(image);
    expectTextureAllocationFailure(output, image);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto texture = context->getResourceManager().createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, DedicatedTextureBindingFailureDestroysImageBeforeItsAllocation)
{
    expectHeadless();
    const auto description = panda::TextureDesc {
        .extent = glm::uvec2 {16, 16},
        .format = ::panda::PixelFormat::Rgba8Unorm,
        .usage = ::panda::TextureUsage::Sampled
    };
    auto* const image = fakeHandle<VkImage>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectTexture(image, description, true);
    expectTextureCleanup(image);
    expectTextureAllocation(memory, image, 0, true);
    expectTextureBinding(image, memory, 0, exhausted);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto texture = context->getResourceManager().createTexture(description);
    ASSERT_FALSE(texture);
    EXPECT_EQ(texture.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(texture.error().native.value_or(panda::NativeError {}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, SharedBuffersRetainNativeStateAndReuseReleasedRanges)
{
    expectHeadless();
    auto* const first = fakeHandle<VkBuffer>(40);
    auto* const second = fakeHandle<VkBuffer>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory);
    expectBinding(second, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto buffer = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(buffer);
    auto copy = *buffer;
    buffer->reset();
    EXPECT_TRUE(copy.isValid());
    EXPECT_EQ(copy.getSize(), 65);
    copy.reset();
    auto replacement = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->getSize(), 65);
}

TEST_F(ContextResourcesIntegration, LiveBuffersRemainBoundAcrossEngineAndHandleMoves)
{
    expectHeadless();
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto created = panda::Engine::create();
    ASSERT_TRUE(created);
    auto buffer = created->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
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
            *output = fakeHandle<VkBuffer>(40);
            return exhausted;
        });
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto buffer =
        context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_FALSE(buffer);
    EXPECT_EQ(buffer.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(buffer.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, FailedBindingReleasesTheRangeAndPreservesTheNativeError)
{
    expectHeadless();
    auto* const first = fakeHandle<VkBuffer>(40);
    auto* const second = fakeHandle<VkBuffer>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory, 0, exhausted);
    expectBinding(second, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto failed =
        context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
    const auto retry = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(retry);
}

TEST_F(ContextResourcesIntegration, RequiredDedicatedBufferIsDestroyedBeforeItsExactAllocation)
{
    expectHeadless();
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(native, 65, true);
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto buffer = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(buffer);
    buffer->reset();
    EXPECT_FALSE(buffer->isValid());
}

TEST_F(ContextResourcesIntegration, LastSharedOwnerControlsNativeDestruction)
{
    expectHeadless();
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    auto lastRelease = testing::MockFunction<void()> {};
    const auto ownerReleased = testing::Expectation {EXPECT_CALL(lastRelease, Call())};
    expectBuffer(native, 65, true, testing::ExpectationSet {ownerReleased});
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto resetComplete = testing::MockFunction<void()> {};
    EXPECT_CALL(resetComplete, Call()).After(memoryCleanup);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto original = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
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
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(native, 65, true);
    expectBufferAllocation(memory, 0, true);
    expectBinding(native, memory, 0, exhausted);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto failed =
        context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(failed.error().native);
    EXPECT_EQ(failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextResourcesIntegration, AllocationFailureDestroysTheUnboundBuffer)
{
    expectHeadless();
    expectBuffer(fakeHandle<VkBuffer>(40));
    EXPECT_CALL(vulkan, vkAllocateMemory(device, NotNull(), nullptr, NotNull()))
        .Times(2)
        .WillRepeatedly(Return(exhausted));
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto failed =
        context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, CopyAndMoveAssignmentReleaseOnlyTheReplacedOwner)
{
    expectHeadless();
    auto* const first = fakeHandle<VkBuffer>(40);
    auto* const second = fakeHandle<VkBuffer>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(first);
    expectBuffer(second);
    expectBufferAllocation(memory);
    expectBinding(first, memory);
    expectBinding(second, memory, 128);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto source = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    auto target = context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
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
    auto* const upload = fakeHandle<VkBuffer>(40);
    auto* const readback = fakeHandle<VkBuffer>(41);
    auto* const uploadMemory = fakeHandle<VkDeviceMemory>(50);
    auto* const readbackMemory = fakeHandle<VkDeviceMemory>(51);
    expectBuffer(upload);
    expectBuffer(readback);
    expectBufferAllocation(uploadMemory, 1);
    expectBufferAllocation(readbackMemory, 2);
    expectHostMapping(uploadMemory);
    expectHostMapping(readbackMemory);
    expectBinding(upload, uploadMemory);
    expectBinding(readback, readbackMemory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    const auto first = context->getResourceManager().createBuffer(
        {.size = 65, .usage = ::panda::BufferUsage::Storage, .memory = panda::BufferMemory::Upload});
    const auto second = context->getResourceManager().createBuffer(
        {.size = 65, .usage = ::panda::BufferUsage::Storage, .memory = panda::BufferMemory::Readback});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
}

using ContextResourcesIntegrationDeathTest = ContextResourcesIntegration;

TEST_F(ContextResourcesIntegrationDeathTest, EngineRejectsDestructionBeforeTheLastResourceOwner)
{
    expectHeadless();
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create().transform([](panda::Engine owner) {
        return std::make_unique<panda::Engine>(std::move(owner));
    });
    ASSERT_TRUE(context);
    auto buffer = (*context)->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(buffer);
    EXPECT_DEATH(context->reset(), "Engine destroyed with live Buffer, Texture, Mesh or OffscreenTarget owners");
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
    const auto context = panda::Engine::create();
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
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::SurfaceLost);
    EXPECT_EQ(context.error().message, "surface lost");
}

TEST_F(ContextResourcesIntegration, NullSurfaceIsRejectedBeforeDeviceSelection)
{
    expectInstance();
    expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {VkSurfaceKHR {}});
    expectParentsDestroyed(false, false);
    const auto context = panda::Engine::createWithSurface(window);
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
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegration, MovedSuspendedEngineRetainsItsDeviceUntilAllFrameResourcesAreDestroyed)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .Times(2)
        .WillRepeatedly(Return(panda::test::WindowSurfaceMock::ExtentResult {glm::uvec2 {}}));
    auto context = panda::Engine::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
    const auto* windowRenderer = &context->getWindowRenderer();
    auto moved = std::move(*context);
    EXPECT_EQ(&moved.getWindowRenderer(), windowRenderer);
    const auto frame = moved.getWindowRenderer().render(panda::RenderView {});
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Suspended);
}

TEST_F(ContextResourcesIntegration, SeparatePresentFamilyProducesTwoDeviceQueueRequests)
{
    expectWindowed(true);
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {glm::uvec2 {}})});
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
}

TEST_F(ContextResourcesIntegration, ExtentQueryFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {
                std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "extent failed")}})});
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "extent failed");
}

TEST_F(ContextResourcesIntegration, GenerationFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(testing::Action<panda::test::WindowSurfaceMock::ExtentResult()> {
            Return(panda::test::WindowSurfaceMock::ExtentResult {glm::uvec2 {640, 480}}
             )
    });
    EXPECT_CALL(generations, createSwapchainGeneration(_))
        .WillOnce(testing::Action<decltype(panda::core::vulkan::createSwapchainGeneration)> {
            [](const panda::core::vulkan::SwapchainGenerationRequest&)
                -> panda::core::vulkan::SwapchainGenerationResult {
                return std::unexpected {panda::core::vulkan::SwapchainGenerationError {
                    .cause = panda::makeError(panda::ErrorCode::ResourceExhausted, "generation failed")}};
            }});
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "generation failed");
}

TEST_F(ContextResourcesIntegration, MovedToEngineRejectsEmptyCompletionOperations)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto moved = std::move(*context);
    const auto empty = panda::CompletionPoint {};
    const auto noWork = moved.getSubmissionManager().poll(empty);
    ASSERT_FALSE(noWork);
    EXPECT_EQ(noWork.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(moved.getSubmissionManager().wait(empty, std::chrono::nanoseconds {0}));
}

TEST_F(ContextResourcesIntegrationDeathTest, MovedFromEngineRejectsResourceAndSubmissionManagerGetters)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto moved = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_DEATH(static_cast<void>(context->getResourceManager()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getSubmissionManager()), "Cannot use a moved-from Engine");
}

TEST_F(ContextResourcesIntegration, SuspendedEngineRejectsEmptyPointsAndNegativeTimeout)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {glm::uvec2 {}}));
    const auto context = panda::Engine::createWithSurface(window);
    ASSERT_TRUE(context);
    const auto empty = panda::CompletionPoint {};
    const auto polled = context->getSubmissionManager().poll(empty);
    ASSERT_FALSE(polled);
    EXPECT_EQ(polled.error().code, panda::ErrorCode::InvalidArgument);
    const auto waited = context->getSubmissionManager().wait(empty, std::chrono::nanoseconds {0});
    ASSERT_FALSE(waited);
    EXPECT_EQ(waited.error().code, panda::ErrorCode::InvalidArgument);
    const auto negative = context->getSubmissionManager().wait(empty, std::chrono::nanoseconds {-1});
    ASSERT_FALSE(negative);
    EXPECT_EQ(negative.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(ContextResourcesIntegration, BatchMovesAcrossEngineAndEmptySubmitRemainsRetryable)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto batch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(batch);
    auto movedEngine = std::move(*context);
    auto movedBatch = std::move(*batch);
    EXPECT_TRUE(movedBatch.isValid());
    EXPECT_FALSE(batch->isValid());
    const auto empty = movedBatch.submit();
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_TRUE(movedBatch.isValid());
    auto replacement = movedEngine.getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(replacement);
    *replacement = std::move(movedBatch);
    EXPECT_TRUE(replacement->isValid());
    // UploadBatch explicitly supports querying its empty moved-from state.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(movedBatch.isValid());
}

TEST_F(ContextResourcesIntegration, FailedTimelineCreationRejectsEngineAndReleasesItsParents)
{
    expectHeadless(vk::Result::eErrorOutOfDeviceMemory);
    expectParentsDestroyed(true, false);
    const auto context = panda::Engine::create();
    ASSERT_FALSE(context);
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextResourcesIntegrationDeathTest, EngineRejectsAnUnsubmittedBatchEvenWithoutBuffers)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto created = panda::Engine::create();
    ASSERT_TRUE(created);
    auto context = std::optional<panda::Engine> {std::move(*created)};
    {
        const auto batch = context->getSubmissionManager().createUploadBatch();
        ASSERT_TRUE(batch);
        EXPECT_DEATH(context.reset(), "Engine destroyed with live upload batches");
    }
}

TEST_F(ContextResourcesIntegration, HeadlessOffscreenSubmissionProducesPairedTimestampSample)
{
    expectHeadless(vk::Result::eSuccess, 64);
    const auto imageDescription = panda::detail::TextureResourceCreateInfo {
        .extent = vk::Extent2D {.width = 16, .height = 16},
        .format = vk::Format::eR8G8B8A8Srgb,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc
    };
    auto* const image = fakeHandle<VkImage>(80);
    auto* const imageView = fakeHandle<VkImageView>(81);
    auto* const imageMemory = fakeHandle<VkDeviceMemory>(82);
    expectTexture(image, imageDescription);
    expectTextureAllocation(imageMemory, image);
    expectTextureBinding(image, imageMemory);
    expectTextureViewCreation(image, imageView, imageDescription);
    expectTextureViewCleanup(image, imageView);

    const auto commandPools = std::array {fakeHandle<VkCommandPool>(90), fakeHandle<VkCommandPool>(91)};
    const auto commandBuffers = std::array {fakeHandle<VkCommandBuffer>(92), fakeHandle<VkCommandBuffer>(93)};
    const auto commandCreationOrder = testing::Sequence {};
    for (auto index = std::size_t {}; index < commandPools.size(); ++index)
    {
        auto* const pool = commandPools.at(index);
        auto* const command = commandBuffers.at(index);
        EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull()))
            .InSequence(commandCreationOrder)
            .WillOnce([pool](VkDevice,
                             const VkCommandPoolCreateInfo* info,
                             const VkAllocationCallbacks*,
                             VkCommandPool* output) {
                EXPECT_EQ(info->queueFamilyIndex, 0U);
                *output = pool;
                return success;
            });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(device, NotNull(), NotNull()))
            .InSequence(commandCreationOrder)
            .WillOnce([pool, command](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
                EXPECT_EQ(info->commandPool, pool);
                EXPECT_EQ(info->commandBufferCount, 1U);
                *output = command;
                return success;
            });
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, pool, nullptr));
    }

    auto* const offscreenQueryPool = fakeHandle<VkQueryPool>(72);
    EXPECT_CALL(vulkan, vkResetCommandPool(device, commandPools.front(), 0)).WillOnce(Return(success));
    EXPECT_CALL(vulkan, vkBeginCommandBuffer(commandBuffers.front(), NotNull())).WillOnce(Return(success));
    EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(commandBuffers.front(), NotNull()));
    EXPECT_CALL(vulkan, vkCmdResetQueryPool(commandBuffers.front(), offscreenQueryPool, 0, 2));
    EXPECT_CALL(vulkan, vkCmdWriteTimestamp2(commandBuffers.front(), _, offscreenQueryPool, 0));
    EXPECT_CALL(vulkan, vkCmdWriteTimestamp2(commandBuffers.front(), _, offscreenQueryPool, 1));
    EXPECT_CALL(vulkan, vkCmdBeginRendering(commandBuffers.front(), NotNull()));
    EXPECT_CALL(vulkan, vkCmdEndRendering(commandBuffers.front()));
    EXPECT_CALL(vulkan, vkEndCommandBuffer(commandBuffers.front())).WillOnce(Return(success));
    EXPECT_CALL(vulkan, vkQueueSubmit2(fakeHandle<VkQueue>(5), 1, NotNull(), VK_NULL_HANDLE))
        .WillOnce([command = commandBuffers.front()](VkQueue, std::uint32_t, const VkSubmitInfo2* info, VkFence) {
            if (info->commandBufferInfoCount != 1U || info->pCommandBufferInfos == nullptr ||
                info->signalSemaphoreInfoCount != 1U || info->pSignalSemaphoreInfos == nullptr)
            {
                ADD_FAILURE() << "Offscreen submit must contain one command buffer and one timeline signal";
                return static_cast<VkResult>(vk::Result::eErrorUnknown);
            }
            EXPECT_EQ(info->pCommandBufferInfos->commandBuffer, command);
            EXPECT_EQ(info->pSignalSemaphoreInfos->value, 1U);
            return success;
        });
    EXPECT_CALL(vulkan, vkGetSemaphoreCounterValue(device, fakeHandle<VkSemaphore>(10), NotNull()))
        .WillOnce([](VkDevice, VkSemaphore, std::uint64_t* value) {
            *value = 1;
            return success;
        });
    EXPECT_CALL(vulkan, vkGetQueryPoolResults(device, offscreenQueryPool, 0, 2, _, NotNull(), _, _))
        .WillOnce([](VkDevice,
                     VkQueryPool,
                     std::uint32_t,
                     std::uint32_t,
                     std::size_t,
                     void* output,
                     VkDeviceSize,
                     VkQueryResultFlags) {
            const auto values = std::array<std::uint64_t, 4> {100, 1, 140, 1};
            const auto valueOutput = std::span {static_cast<std::uint64_t*>(output), values.size()};
            std::ranges::copy(values, valueOutput.begin());
            return success;
        });

    expectParentsDestroyed(true, false);
    auto created = panda::Engine::create({.enableFramePerformanceSampling = true});
    ASSERT_TRUE(created) << created.error().message;
    auto engine = std::optional<panda::Engine> {std::move(*created)};
    auto target = engine->getResourceManager().createOffscreenTarget({
        .extent = {16, 16}
    });
    ASSERT_TRUE(target) << target.error().message;
    const auto frame = engine->getOffscreenRenderer().render(panda::RenderView {}, *target);
    ASSERT_TRUE(frame) << frame.error().message;
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Rendered);
    const auto samples = engine->getPerformanceManager().takeAvailableSamples();
    ASSERT_TRUE(samples) << samples.error().message;
    ASSERT_EQ(samples->size(), 1U);
    EXPECT_EQ(samples->front().gpuExecutionDuration.count(), 100);
    target->reset();
    engine.reset();
}

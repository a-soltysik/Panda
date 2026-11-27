// clang-format off
#ifdef VK_USE_PLATFORM_WIN32_KHR
#    include <Windows.h> // IWYU pragma: keep
#    include <winnt.h>
#endif
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#ifdef VK_USE_PLATFORM_WIN32_KHR
#    include <vulkan/vulkan_win32.h>
#endif
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <panda/interop/ExternalBufferManager.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/submission/SubmissionManager.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

struct TimestampScenario
{
    std::uint32_t validBits {0};
    float periodNanoseconds {0.0F};
};

struct BorrowedEngineServices
{
    const panda::ResourceManager* resourceManager {nullptr};
    const panda::SubmissionManager* submissionManager {nullptr};
    const panda::OffscreenRenderer* offscreenRenderer {nullptr};
    panda::PerformanceManager* performanceManager {nullptr};
    panda::ExternalBufferManager* externalBufferManager {nullptr};
};

auto borrowServices(panda::Engine& engine) -> BorrowedEngineServices
{
    return {.resourceManager = &engine.getResourceManager(),
            .submissionManager = &engine.getSubmissionManager(),
            .offscreenRenderer = &engine.getOffscreenRenderer(),
            .performanceManager = &engine.getPerformanceManager(),
            .externalBufferManager = &engine.getExternalBufferManager()};
}

void expectSameServices(panda::Engine& engine, const BorrowedEngineServices& services)
{
    EXPECT_EQ(&engine.getResourceManager(), services.resourceManager);
    EXPECT_EQ(&engine.getSubmissionManager(), services.submissionManager);
    EXPECT_EQ(&engine.getOffscreenRenderer(), services.offscreenRenderer);
    EXPECT_EQ(&engine.getPerformanceManager(), services.performanceManager);
    EXPECT_EQ(&engine.getExternalBufferManager(), services.externalBufferManager);
}

auto extension(std::string_view name) -> VkExtensionProperties
{
    auto value = VkExtensionProperties {};
    std::ranges::copy(name, std::span {value.extensionName}.begin());
    return value;
}

auto externalMemoryExtensionName() -> std::string_view
{
#ifdef _WIN32
    return "VK_KHR_external_memory_win32";
#else
    return "VK_KHR_external_memory_fd";
#endif
}

auto externalSemaphoreExtensionName() -> std::string_view
{
#ifdef _WIN32
    return "VK_KHR_external_semaphore_win32";
#else
    return "VK_KHR_external_semaphore_fd";
#endif
}

auto externalSemaphoreHandleType() -> VkExternalSemaphoreHandleTypeFlagBits
{
#ifdef _WIN32
    return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

auto externalMemoryHandleType() -> VkExternalMemoryHandleTypeFlagBits
{
#ifdef _WIN32
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

void expectBufferExternalMemoryInfo(const void* next, bool external)
{
    EXPECT_EQ(next != nullptr, external);
    if (external)
    {
        const auto* const exportInfo = static_cast<const VkExternalMemoryBufferCreateInfo*>(next);
        EXPECT_EQ(exportInfo->handleTypes, externalMemoryHandleType());
    }
}

auto hasDeviceExtension(const VkDeviceCreateInfo& info, std::string_view expected) -> bool
{
    const auto names = std::span {info.ppEnabledExtensionNames, info.enabledExtensionCount};
    return std::ranges::any_of(names, [expected](const char* name) {
        return std::string_view {name} == expected;
    });
}

void verifyCommonDeviceFeatures(const VkPhysicalDeviceVulkan12Features& vulkan12,
                                const VkPhysicalDeviceVulkan13Features& vulkan13)
{
    EXPECT_EQ(vulkan12.timelineSemaphore, vk::True);
    EXPECT_EQ(vulkan13.dynamicRendering, vk::True);
    EXPECT_EQ(vulkan13.synchronization2, vk::True);
}

void verifySwapchainMaintenanceFeature(const VkPhysicalDeviceVulkan13Features& vulkan13)
{
    const auto* const maintenance =
        static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13.pNext);
    EXPECT_EQ(maintenance->swapchainMaintenance1, vk::True);
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
    verifyCommonDeviceFeatures(*vulkan12, *vulkan13);
    if (windowed)
    {
        ASSERT_NE(vulkan13->pNext, nullptr);
        verifySwapchainMaintenanceFeature(*vulkan13);
    }
    else
    {
        EXPECT_EQ(vulkan13->pNext, nullptr);
    }
}

void setDriverProperties(const VkPhysicalDeviceProperties2& output)
{
    auto* const driver = static_cast<VkPhysicalDeviceDriverProperties*>(output.pNext);
    ASSERT_NE(driver, nullptr);
    std::ranges::copy(std::string_view {"Unit Vulkan driver"}, std::span {driver->driverName}.begin());
    std::ranges::copy(std::string_view {"Unit driver info"}, std::span {driver->driverInfo}.begin());
}

// Explicit Action types avoid gMock's OnceAction ownership adapter, which
// triggers a spurious leak diagnostic in clang-tidy with MSVC headers.
class EngineLifecycleTest : public testing::Test
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

    void expectAdapter(bool windowed,
                       bool separateQueues = false,
                       TimestampScenario timestamps = {},
                       bool hdrBlendSupport = true)
    {
        expectEnumeration(timestamps.periodNanoseconds);
        expectRenderFormatSupport(hdrBlendSupport);
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFeatures2(physical, NotNull()))
            .WillOnce(testing::Action<decltype(vkGetPhysicalDeviceFeatures2)> {
                [windowed](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
                    ASSERT_NE(features, nullptr);
                    ASSERT_TRUE(panda::test::enableRequiredVulkanFeatures(*features));
                    auto* const vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(
                        static_cast<VkPhysicalDeviceVulkan12Features*>(features->pNext)->pNext);
                    if (windowed)
                    {
                        auto* const maintenance =
                            static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
                        maintenance->swapchainMaintenance1 = vk::True;
                    }
                }});
        expectQueueFamilies(separateQueues, timestamps.validBits);
        if (windowed)
        {
            expectPresentationSupport(separateQueues);
        }
    }

    void expectRenderFormatSupport(bool blendSupport = true)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16B16A16_SFLOAT, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([blendSupport](VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
                properties->optimalTilingFeatures =
                    static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) |
                    (blendSupport ? static_cast<VkFormatFeatureFlags>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                                  : VkFormatFeatureFlags {}) |
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

    void expectEnumeration(float timestampPeriod)
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
            .WillRepeatedly([timestampPeriod](VkPhysicalDevice, VkPhysicalDeviceProperties* output) {
                *output = VkPhysicalDeviceProperties {};
                output->apiVersion = vk::ApiVersion13;
                output->deviceType = static_cast<VkPhysicalDeviceType>(vk::PhysicalDeviceType::eDiscreteGpu);
                output->limits.maxStorageBufferRange = 4096;
                output->limits.maxDescriptorSetStorageBuffers = 4;
                output->limits.maxPerStageDescriptorStorageBuffers = 4;
                output->limits.timestampPeriod = timestampPeriod;
                std::ranges::copy(std::string_view {"Unit GPU"}, std::span {output->deviceName}.begin());
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(physical, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                setDriverProperties(*output);
            });
    }

    void expectQueueFamilies(bool separate, std::uint32_t timestampValidBits)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceQueueFamilyProperties(physical, NotNull(), _))
            .Times(2)
            .WillRepeatedly([separate, timestampValidBits](VkPhysicalDevice,
                                                           std::uint32_t* count,
                                                           VkQueueFamilyProperties* output) {
                *count = separate ? 2U : 1U;
                if (output == nullptr)
                {
                    return;
                }
                auto families = std::span {output, *count};
                families.front() = VkQueueFamilyProperties {
                    .queueFlags = static_cast<VkQueueFlags>(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute),
                    .queueCount = 1,
                    .timestampValidBits = timestampValidBits,
                    .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
                };
                if (separate)
                {
                    families.back() = VkQueueFamilyProperties {
                        .queueFlags = static_cast<VkQueueFlags>(vk::QueueFlagBits::eTransfer),
                        .queueCount = 1,
                        .timestampValidBits = 0,
                        .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
                    };
                }
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

    void expectExternalMemoryExtensions(bool available, bool semaphoreAvailable = false)
    {
        auto entries = std::vector<VkExtensionProperties> {};
        if (available)
        {
            entries.push_back(extension(externalMemoryExtensionName()));
            if (semaphoreAvailable)
            {
                entries.push_back(extension(externalSemaphoreExtensionName()));
            }
        }
        EXPECT_CALL(vulkan, vkEnumerateDeviceExtensionProperties(physical, nullptr, NotNull(), _))
            .Times(available ? 2 : 1)
            .WillRepeatedly(
                [entries](VkPhysicalDevice, const char*, std::uint32_t* count, VkExtensionProperties* output) {
                    *count = static_cast<std::uint32_t>(entries.size());
                    if (output != nullptr)
                    {
                        std::ranges::copy(entries, output);
                    }
                    return success;
                });
    }

    void expectTimelineSemaphoreSupport(bool supported)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceExternalSemaphoreProperties(physical, NotNull(), NotNull()))
            .WillOnce([supported](VkPhysicalDevice,
                                  const VkPhysicalDeviceExternalSemaphoreInfo* info,
                                  VkExternalSemaphoreProperties* properties) {
                EXPECT_EQ(info->handleType, externalSemaphoreHandleType());
                ASSERT_NE(info->pNext, nullptr);
                const auto* const semaphoreType = static_cast<const VkSemaphoreTypeCreateInfo*>(info->pNext);
                EXPECT_EQ(semaphoreType->semaphoreType, VK_SEMAPHORE_TYPE_TIMELINE);
                // Model binary-only drivers as exportable only when the query asks for binary semaphores.
                if (supported || semaphoreType->semaphoreType == VK_SEMAPHORE_TYPE_BINARY)
                {
                    properties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
                    properties->compatibleHandleTypes = info->handleType;
                }
            });
    }

    void expectSubmissionQueue(VkResult result = success)
    {
        auto* const queue = fakeHandle<VkQueue>(30);
        auto* const semaphore = fakeHandle<VkSemaphore>(31);
        EXPECT_CALL(vulkan, vkGetDeviceQueue(device, 0, 0, NotNull())).WillOnce(testing::SetArgPointee<3>(queue));
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce([semaphore, result](VkDevice,
                                          const VkSemaphoreCreateInfo* info,
                                          const VkAllocationCallbacks*,
                                          VkSemaphore* output) {
                EXPECT_NE(info->pNext, nullptr);
                if (info->pNext == nullptr)
                {
                    return success;
                }
                const auto* const type = static_cast<const VkSemaphoreTypeCreateInfo*>(info->pNext);
                EXPECT_EQ(type->semaphoreType, VK_SEMAPHORE_TYPE_TIMELINE);
                EXPECT_EQ(type->initialValue, 0U);
                if (result == success)
                {
                    *output = semaphore;
                }
                return result;
            });
        if (result == success)
        {
            EXPECT_CALL(vulkan, vkDestroySemaphore(device, semaphore, nullptr));
        }
    }

    static void expectDeviceExtensions(const VkDeviceCreateInfo& info,
                                       bool windowed,
                                       bool externalMemory,
                                       bool externalSemaphore)
    {
        EXPECT_EQ(info.enabledExtensionCount,
                  (windowed ? 2U : 0U) + (externalMemory ? 1U : 0U) + (externalSemaphore ? 1U : 0U));
        if (windowed && info.enabledExtensionCount > 1U)
        {
            const auto names = std::span {info.ppEnabledExtensionNames, info.enabledExtensionCount};
            EXPECT_EQ(std::string_view {names.front()}, vk::KHRSwapchainExtensionName);
            EXPECT_EQ(std::string_view {names.subspan(1).front()}, vk::KHRSwapchainMaintenance1ExtensionName);
        }
        if (externalMemory)
        {
            EXPECT_TRUE(hasDeviceExtension(info, externalMemoryExtensionName()));
        }
        if (externalSemaphore)
        {
            EXPECT_TRUE(hasDeviceExtension(info, externalSemaphoreExtensionName()));
        }
    }

    void expectDevice(bool windowed,
                      VkResult result = success,
                      bool separateQueues = false,
                      bool externalMemory = false,
                      bool externalSemaphore = false,
                      VkResult timelineResult = success)
    {
        if (result == success)
        {
            expectMemoryPool();
            if (!windowed)
            {
                expectSubmissionQueue(timelineResult);
            }
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
            vkGetPhysicalDeviceProperties2(physical, testing::Truly([](const VkPhysicalDeviceProperties2* info) {
                                               const auto* const chain =
                                                   static_cast<const VkBaseOutStructure*>(info->pNext);
                                               return chain != nullptr &&
                                                      chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
                                           })))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* info) {
                auto* const identity = static_cast<VkPhysicalDeviceIDProperties*>(info->pNext);
                identity->deviceUUID[0] = 1;
            });
        EXPECT_CALL(vulkan, vkCreateDevice(physical, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::Action<decltype(vkCreateDevice)> {
                [this, windowed, result, separateQueues, externalMemory, externalSemaphore](
                    VkPhysicalDevice,
                    const VkDeviceCreateInfo* info,
                    const VkAllocationCallbacks*,
                    VkDevice* output) {
                    verifyDeviceFeatures(*info, windowed);
                    EXPECT_EQ(info->queueCreateInfoCount, separateQueues ? 2U : 1U);
                    expectDeviceExtensions(*info, windowed, externalMemory, externalSemaphore);
                    *output = device;
                    return result;
                }});
    }

    void expectPerformancePools()
    {
        const auto queryPools = std::array {fakeHandle<VkQueryPool>(40),
                                            fakeHandle<VkQueryPool>(41),
                                            fakeHandle<VkQueryPool>(42),
                                            fakeHandle<VkQueryPool>(43)};
        const auto expectPoolCreation = [this](VkQueryPool queryPool) {
            EXPECT_CALL(vulkan, vkCreateQueryPool(device, NotNull(), nullptr, NotNull()))
                .WillOnce([queryPool](VkDevice,
                                      const VkQueryPoolCreateInfo* info,
                                      const VkAllocationCallbacks*,
                                      VkQueryPool* output) {
                    EXPECT_EQ(info->queryType, VK_QUERY_TYPE_TIMESTAMP);
                    EXPECT_EQ(info->queryCount, 2U);
                    *output = queryPool;
                    return success;
                });
        };
        {
            const auto sequence = testing::InSequence {};
            for (auto* const queryPool : queryPools)
            {
                expectPoolCreation(queryPool);
            }
        }
        for (auto* const queryPool : queryPools)
        {
            EXPECT_CALL(vulkan, vkDestroyQueryPool(device, queryPool, nullptr));
        }
    }

    void expectParentsDestroyed(bool hasDevice, bool hasSurface)
    {
        auto parents = testing::ExpectationSet {};
        if (hasDevice)
        {
            parents += EXPECT_CALL(vulkan, vkDestroyDevice(device, nullptr)).After(frameCleanup).After(memoryCleanup);
        }
        if (hasSurface)
        {
            parents += EXPECT_CALL(vulkan, vkDestroySurfaceKHR(instance, surface, nullptr));
        }
        EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr)).After(parents);
    }

    void expectFrameResources(bool failOffscreenInitialization = false)
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
        auto& commandPoolCalls = EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull()));
        commandPoolCalls
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkCommandPool>(20)), Return(success))})
            .WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                testing::DoAll(testing::SetArgPointee<3>(fakeHandle<VkCommandPool>(21)), Return(success))});
        if (failOffscreenInitialization)
        {
            commandPoolCalls.WillOnce(testing::Action<decltype(vkCreateCommandPool)> {
                [](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*) {
                    return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
                }});
        }
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

    void expectWindowed(bool separateQueues = false, bool failOffscreenInitialization = false)
    {
        expectInstance();
        expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {surface});
        expectAdapter(true, separateQueues);
        expectDevice(true, success, separateQueues);
        expectFrameResources(failOffscreenInitialization);
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
                if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
                {
                    setDriverProperties(*output);
                    return;
                }
                if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES)
                {
                    auto* timeline = static_cast<VkPhysicalDeviceTimelineSemaphoreProperties*>(output->pNext);
                    timeline->maxTimelineSemaphoreValueDifference = std::numeric_limits<std::uint64_t>::max();
                    return;
                }
                if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)
                {
                    auto* id = static_cast<VkPhysicalDeviceIDProperties*>(output->pNext);
                    id->deviceUUID[0] = 1;
                    return;
                }
                output->properties.limits.maxMemoryAllocationCount = 16;
                output->properties.limits.nonCoherentAtomSize = 64;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = std::uint64_t {128} * 1024 * 1024;
            });
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
                      const testing::ExpectationSet& ownerReleased = {},
                      bool external = false)
    {
        EXPECT_CALL(vulkan, vkCreateBuffer(device, NotNull(), nullptr, NotNull()))
            .InSequence(bufferCreationOrder)
            .WillOnce([buffer, size, external](VkDevice,
                                               const VkBufferCreateInfo* info,
                                               const VkAllocationCallbacks*,
                                               VkBuffer* output) {
                EXPECT_EQ(info->size, size);
                EXPECT_EQ(info->usage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                EXPECT_EQ(info->sharingMode, VK_SHARING_MODE_EXCLUSIVE);
                EXPECT_EQ(info->flags, 0);
                expectBufferExternalMemoryInfo(info->pNext, external);
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

    void expectOffscreenFormat(VkImageUsageFlags usage)
    {
        EXPECT_CALL(vulkan,
                    vkGetPhysicalDeviceImageFormatProperties(physical,
                                                             VK_FORMAT_R8G8B8A8_SRGB,
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             usage,
                                                             0,
                                                             NotNull()))
            .WillOnce([](VkPhysicalDevice,
                         VkFormat,
                         VkImageType,
                         VkImageTiling,
                         VkImageUsageFlags,
                         VkImageCreateFlags,
                         VkImageFormatProperties* output) {
                output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                return success;
            });
    }

    void expectOffscreenImageCreation(VkImage image, VkImageUsageFlags usage)
    {
        EXPECT_CALL(vulkan, vkCreateImage(device, NotNull(), nullptr, NotNull()))
            .WillOnce(
                [image, usage](VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* output) {
                    EXPECT_EQ(info->format, VK_FORMAT_R8G8B8A8_SRGB);
                    EXPECT_EQ(info->usage, usage);
                    *output = image;
                    return success;
                });
    }

    void expectOffscreenImageRequirements(VkImage image)
    {
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(device, NotNull(), NotNull()))
            .WillOnce([image](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                EXPECT_EQ(info->image, image);
                output->memoryRequirements = {.size = 256, .alignment = 64, .memoryTypeBits = 1};
                auto* const dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(dedicated, nullptr);
                dedicated->requiresDedicatedAllocation = vk::False;
                dedicated->prefersDedicatedAllocation = vk::False;
            });
    }

    void expectOffscreenImageBindingAndView(VkImage image, VkImageView view, VkDeviceMemory memory)
    {
        EXPECT_CALL(vulkan, vkBindImageMemory2(device, 1, NotNull()))
            .WillOnce([image, memory](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, 0U);
                return success;
            });
        EXPECT_CALL(vulkan, vkCreateImageView(device, NotNull(), nullptr, NotNull()))
            .WillOnce(
                [image,
                 view](VkDevice, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* output) {
                    EXPECT_EQ(info->image, image);
                    *output = view;
                    return success;
                });
        EXPECT_CALL(vulkan, vkDestroyImageView(device, view, nullptr));
        EXPECT_CALL(vulkan, vkDestroyImage(device, image, nullptr));
    }

    void expectOffscreenTarget(VkImage image, VkImageView view, VkDeviceMemory memory)
    {
        constexpr auto usage = static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
                               static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        expectOffscreenFormat(usage);
        expectOffscreenImageCreation(image, usage);
        expectOffscreenImageRequirements(image);
        expectBufferAllocation(memory);
        expectOffscreenImageBindingAndView(image, view, memory);
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

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    testing::StrictMock<panda::test::WindowSurfaceMock> window;
    VkInstance instance {fakeHandle<VkInstance>(1)};
    VkPhysicalDevice physical {fakeHandle<VkPhysicalDevice>(2)};
    VkDevice device {fakeHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeHandle<VkSurfaceKHR>(4)};
    testing::ExpectationSet frameCleanup;
    testing::ExpectationSet bufferCleanup;
    testing::ExpectationSet memoryCleanup;
    testing::Sequence bufferCreationOrder;
    testing::Sequence bindingOrder;
    testing::Sequence allocationOrder;
};
}

TEST_F(EngineLifecycleTest, CreatesEngineWithZeroLightBudget)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);
    const auto created = panda::Engine::create({.maxLights = 0});

    ASSERT_TRUE(created);
}

TEST_F(EngineLifecycleTest, RejectsAdapterWithoutHdrBlendSupportBeforeCreatingDevice)
{
    expectInstance();
    expectAdapter(false, false, {}, false);
    expectParentsDestroyed(false, false);
    EXPECT_CALL(vulkan, vkCreateDevice(_, NotNull(), nullptr, NotNull())).Times(0);

    const auto created = panda::Engine::create();

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(EngineLifecycleTest, TimelineCreationFailurePreventsEnginePublication)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false, success, false, false, false, exhausted);
    expectParentsDestroyed(true, false);

    const auto created = panda::Engine::create();

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
    const auto& native = created.error().native;
    ASSERT_TRUE(native.has_value()) << "Expected the native Vulkan error to be preserved";
    EXPECT_EQ(native.value().code, exhausted);
}

TEST_F(EngineLifecycleTest, CreatesHeadlessQueueAndUploadManagerWithoutGpuAllocations)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);
    EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull())).Times(0);
    EXPECT_CALL(vulkan, vkCreateBuffer(device, NotNull(), nullptr, NotNull())).Times(0);
    EXPECT_CALL(vulkan, vkCreateImage(device, NotNull(), nullptr, NotNull())).Times(0);

    auto engine = panda::Engine::create();
    ASSERT_TRUE(engine);
    const auto batch = engine->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(batch);
}

TEST_F(EngineLifecycleTest, CaptureInitializationFailureDoesNotAcquireWindowImage)
{
    auto* const image = fakeHandle<VkImage>(40);
    auto* const imageView = fakeHandle<VkImageView>(41);
    auto* const memory = fakeHandle<VkDeviceMemory>(42);
    expectWindowed(false, true);
    expectOffscreenTarget(image, imageView, memory);
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {glm::uvec2 {}}));
    EXPECT_CALL(vulkan, vkAcquireNextImageKHR(_, _, _, _, _, _)).Times(0);

    const auto engine = panda::Engine::createWithSurface(window);
    ASSERT_TRUE(engine) << engine.error().message;
    EXPECT_TRUE(engine->getFeatures().windowRenderingEnabled);
    EXPECT_FALSE(engine->getFeatures().validationEnabled);
    const auto target = engine->getResourceManager().createOffscreenTarget({
        .extent = {32, 32}
    });
    ASSERT_TRUE(target);
    const auto result = engine->getWindowRenderer().renderWithCapture(panda::RenderView {}, *target);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(EngineLifecycleTest, ReportsTimestampSupportWithoutCreatingAnInactivePerformanceManager)
{
    expectInstance();
    expectAdapter(false, false, TimestampScenario {.validBits = 64, .periodNanoseconds = 2.5F});
    expectDevice(false);
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);

    const auto capabilities = context->getFeatures().performance;
    EXPECT_TRUE(capabilities.hasGpuTimestampQueries);
    EXPECT_FALSE(capabilities.isGpuTimestampSamplingEnabled);
    EXPECT_DEATH(static_cast<void>(context->getPerformanceManager()), "Performance sampling is not enabled");
}

TEST_F(EngineLifecycleTest, DoesNotCreatePerformanceManagerWhenTimestampQueriesAreUnsupported)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create({.enableFramePerformanceSampling = true});
    ASSERT_TRUE(context);

    const auto capabilities = context->getFeatures().performance;
    EXPECT_FALSE(capabilities.hasGpuTimestampQueries);
    EXPECT_FALSE(capabilities.isGpuTimestampSamplingEnabled);
    EXPECT_DEATH(static_cast<void>(context->getPerformanceManager()), "Performance sampling is not enabled");
}

TEST_F(EngineLifecycleTest, ReportsOptionalExternalBufferCapabilityAtStartup)
{
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(true);
    expectDevice(false, success, false, true);
    expectParentsDestroyed(true, false);

    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});

    ASSERT_TRUE(context);
    EXPECT_TRUE(context->getFeatures().externalBufferExportEnabled);
    EXPECT_EQ(context->getDeviceInfo().deviceUuid.at(0), 1U);
}

TEST_F(EngineLifecycleTest, ExportedBufferUsesTheEngineDeviceUuid)
{
    auto* const nativeBuffer = fakeHandle<VkBuffer>(60);
    auto* const memory = fakeHandle<VkDeviceMemory>(61);
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(true);
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceExternalBufferProperties(physical, NotNull(), NotNull()))
        .WillOnce([](VkPhysicalDevice,
                     const VkPhysicalDeviceExternalBufferInfo* info,
                     VkExternalBufferProperties* properties) {
            properties->externalMemoryProperties.externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
            properties->externalMemoryProperties.compatibleHandleTypes = info->handleType;
        });
    expectBuffer(nativeBuffer, 65, true, {}, true);
    expectBufferAllocation(memory, 0, true);
    expectBinding(nativeBuffer, memory);
#ifdef _WIN32
    EXPECT_CALL(vulkan, vkGetMemoryWin32HandleKHR(device, NotNull(), NotNull()))
        .WillOnce([](VkDevice, const VkMemoryGetWin32HandleInfoKHR*, HANDLE* output) {
            *output = nullptr;
            return success;
        });
#else
    EXPECT_CALL(vulkan, vkGetMemoryFdKHR(device, NotNull(), NotNull()))
        .WillOnce([](VkDevice, const VkMemoryGetFdInfoKHR*, int* output) {
            *output = -1;
            return success;
        });
#endif
    expectDevice(false, success, false, true);
    expectParentsDestroyed(true, false);

    auto engine =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});
    ASSERT_TRUE(engine);
    auto exported =
        engine->getExternalBufferManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});

    ASSERT_TRUE(exported);
    EXPECT_EQ(engine->getDeviceInfo().deviceUuid.at(0), 1U);
    EXPECT_EQ(exported->getDeviceUuid(), engine->getDeviceInfo().deviceUuid);
}

TEST_F(EngineLifecycleTest, RejectsExternalBufferOperationsWhenExportIsUnavailable)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);

    auto& externalBuffers = context->getExternalBufferManager();
    const auto created = externalBuffers.createBuffer({.size = 1, .usage = ::panda::BufferUsage::Storage});
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::Unsupported);
    const auto released = externalBuffers.releaseToExternal(panda::Buffer {});
    ASSERT_FALSE(released);
    EXPECT_EQ(released.error().code, panda::ErrorCode::Unsupported);
    const auto acquired = externalBuffers.acquireFromExternal(panda::Buffer {}, panda::ExternalTimelineValue {1});
    ASSERT_FALSE(acquired);
    EXPECT_EQ(acquired.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(EngineLifecycleTest, RejectsEmptyHandoffsWhenMemoryExportIsAvailable)
{
    auto* const native = fakeHandle<VkBuffer>(42);
    auto* const memory = fakeHandle<VkDeviceMemory>(52);
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(true);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectDevice(false, success, false, true);
    expectParentsDestroyed(true, false);
    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});
    ASSERT_TRUE(context);

    auto& externalBuffers = context->getExternalBufferManager();
    const auto released = externalBuffers.releaseToExternal(panda::Buffer {});
    ASSERT_FALSE(released);
    EXPECT_EQ(released.error().code, panda::ErrorCode::InvalidArgument);
    const auto acquired = externalBuffers.acquireFromExternal(panda::Buffer {}, panda::ExternalTimelineValue {1});
    ASSERT_FALSE(acquired);
    EXPECT_EQ(acquired.error().code, panda::ErrorCode::InvalidArgument);

    const auto ordinary =
        context->getResourceManager().createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(ordinary);
    const auto ordinaryRelease = externalBuffers.releaseToExternal(*ordinary);
    ASSERT_FALSE(ordinaryRelease);
    EXPECT_EQ(ordinaryRelease.error().code, panda::ErrorCode::InvalidArgument);
    const auto ordinaryAcquire = externalBuffers.acquireFromExternal(*ordinary, panda::ExternalTimelineValue {1});
    ASSERT_FALSE(ordinaryAcquire);
    EXPECT_EQ(ordinaryAcquire.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(EngineLifecycleTest, ReportsOptionalExternalBufferCapabilityUnavailableAtStartup)
{
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);

    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});

    ASSERT_TRUE(context);
    EXPECT_FALSE(context->getFeatures().externalBufferExportEnabled);
}

TEST_F(EngineLifecycleTest, RequiresExternalBufferCapabilityBeforePublishingEngine)
{
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(false);
    expectParentsDestroyed(false, false);

    const auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Required});

    ASSERT_FALSE(context);
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(EngineLifecycleTest, EnablesTimelineSemaphoreExportWhenTheTimelineQueryIsSupported)
{
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(true, true);
    expectTimelineSemaphoreSupport(true);
    expectDevice(false, success, false, true, true);
    expectParentsDestroyed(true, false);

    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});

    ASSERT_TRUE(context);
    EXPECT_TRUE(context->getFeatures().externalBufferExportEnabled);
    EXPECT_TRUE(context->getFeatures().externalSemaphoreExportEnabled);
}

TEST_F(EngineLifecycleTest, DoesNotEnableBinaryOnlyExternalSemaphoreSupportForTimelineBuffers)
{
    expectInstance();
    expectAdapter(false);
    expectExternalMemoryExtensions(true, true);
    expectTimelineSemaphoreSupport(false);
    expectDevice(false, success, false, true);
    expectParentsDestroyed(true, false);

    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Optional});

    ASSERT_TRUE(context);
    EXPECT_TRUE(context->getFeatures().externalBufferExportEnabled);
    EXPECT_FALSE(context->getFeatures().externalSemaphoreExportEnabled);
}

TEST_F(EngineLifecycleTest, InitializesOptInSamplingAndReportsItThroughEngineFeatures)
{
    expectInstance();
    expectAdapter(false, false, TimestampScenario {.validBits = 64, .periodNanoseconds = 2.5F});
    expectDevice(false);
    expectPerformancePools();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create({.enableFramePerformanceSampling = true});
    ASSERT_TRUE(context);

    const auto capabilities = context->getFeatures().performance;
    EXPECT_TRUE(capabilities.hasGpuTimestampQueries);
    EXPECT_TRUE(capabilities.isGpuTimestampSamplingEnabled);
    auto& performance = context->getPerformanceManager();
    const auto latest = performance.getLatestSample();
    ASSERT_TRUE(latest);
    EXPECT_FALSE(*latest);
    const auto next = performance.takeNextSample();
    ASSERT_TRUE(next);
    EXPECT_FALSE(*next);
    const auto all = performance.takeAvailableSamples();
    ASSERT_TRUE(all);
    EXPECT_TRUE(all->empty());
}

TEST_F(EngineLifecycleTest, BorrowedServicesRemainStableAcrossEngineMoves)
{
    expectInstance();
    expectAdapter(false, false, TimestampScenario {.validBits = 64, .periodNanoseconds = 2.5F});
    expectDevice(false);
    expectPerformancePools();
    auto* const native = fakeHandle<VkBuffer>(40);
    auto* const memory = fakeHandle<VkDeviceMemory>(50);
    expectBuffer(native);
    expectBufferAllocation(memory);
    expectBinding(native, memory);
    expectParentsDestroyed(true, false);
    auto services = std::optional<BorrowedEngineServices> {};
    auto movedEngine = std::unique_ptr<panda::Engine> {};
    {
        auto created = panda::Engine::create({.enableFramePerformanceSampling = true});
        ASSERT_TRUE(created);
        services.emplace(borrowServices(*created));
        movedEngine = std::make_unique<panda::Engine>(std::move(*created));
    }

    expectSameServices(*movedEngine, *services);
    auto buffer = services->resourceManager->createBuffer({.size = 65, .usage = ::panda::BufferUsage::Storage});
    ASSERT_TRUE(buffer);
    buffer->reset();
    const auto batch = services->submissionManager->createUploadBatch();
    ASSERT_TRUE(batch);
    EXPECT_FALSE(services->offscreenRenderer->render({}, {}));
    const auto latest = services->performanceManager->getLatestSample();
    ASSERT_TRUE(latest);
    EXPECT_FALSE(*latest);
    const auto unsupported = services->externalBufferManager->createBuffer({});
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

using EngineLifecycleDeathTest = EngineLifecycleTest;

TEST_F(EngineLifecycleDeathTest, MovedFromEngineRejectsCommonGetters)
{
    expectHeadless();
    expectParentsDestroyed(true, false);
    auto context = panda::Engine::create();
    ASSERT_TRUE(context);
    auto moved = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_DEATH(static_cast<void>(context->getDeviceInfo()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getResourceManager()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getSubmissionManager()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getOffscreenRenderer()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getPerformanceManager()), "Cannot use a moved-from Engine");
    EXPECT_DEATH(static_cast<void>(context->getExternalBufferManager()), "Cannot use a moved-from Engine");
    auto movedAgain = std::move(*context);
    EXPECT_FALSE(movedAgain.isValid());
}

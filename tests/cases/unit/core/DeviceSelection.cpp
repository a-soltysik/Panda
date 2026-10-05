#include "DeviceSelection.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <panda/Context.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "LogCapture.hpp"
#include "ScopedMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using testing::_;
using testing::NotNull;
using testing::Return;

template <typename Handle>
auto fakeHandle(std::uintptr_t value) -> Handle
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        return reinterpret_cast<Handle>(value);
    }
    else
    {
        return static_cast<Handle>(value);
    }
}

auto makeExtension(std::string_view name) -> VkExtensionProperties
{
    auto extension = VkExtensionProperties {};
    std::ranges::copy(name, std::span {extension.extensionName}.begin());
    return extension;
}

struct DeviceScenario
{
    VkPhysicalDevice brokenDevice;
    VkPhysicalDevice usableDevice;
    VkInstance instance;
    VkSurfaceKHR surface;
    std::array<VkExtensionProperties, 2> extensions;
};

auto makeDeviceScenario() -> DeviceScenario
{
    return DeviceScenario {
        .brokenDevice = fakeHandle<VkPhysicalDevice>(1),
        .usableDevice = fakeHandle<VkPhysicalDevice>(2),
        .instance = fakeHandle<VkInstance>(3),
        .surface = fakeHandle<VkSurfaceKHR>(4),
        .extensions = {makeExtension(vk::KHRSwapchainExtensionName),
                       makeExtension(vk::KHRSwapchainMaintenance1ExtensionName)}
    };
}

void expectPhysicalDevices(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkEnumeratePhysicalDevices(_, NotNull(), _))
        .WillRepeatedly([&scenario](VkInstance, std::uint32_t* count, VkPhysicalDevice* devices) {
            *count = 2;
            if (devices != nullptr)
            {
                devices[0] = scenario.brokenDevice;
                devices[1] = scenario.usableDevice;
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectDeviceProperties(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
        .WillRepeatedly([&scenario](VkPhysicalDevice device, VkPhysicalDeviceProperties* properties) {
            *properties = VkPhysicalDeviceProperties {};
            properties->apiVersion = vk::ApiVersion13;
            properties->deviceType = static_cast<VkPhysicalDeviceType>(vk::PhysicalDeviceType::eDiscreteGpu);
            const auto name =
                device == scenario.brokenDevice ? std::string_view {"Broken GPU"} : std::string_view {"Usable GPU"};
            std::ranges::copy(name, std::span {properties->deviceName}.begin());
        });
}

void expectDeviceExtensions(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkEnumerateDeviceExtensionProperties(_, nullptr, NotNull(), _))
        .WillRepeatedly(
            [&scenario](VkPhysicalDevice device, const char*, std::uint32_t* count, VkExtensionProperties* extensions) {
                if (device == scenario.brokenDevice)
                {
                    *count = 0;
                    return static_cast<VkResult>(vk::Result::eErrorUnknown);
                }
                *count = static_cast<std::uint32_t>(scenario.extensions.size());
                if (extensions != nullptr)
                {
                    std::ranges::copy(scenario.extensions, extensions);
                }
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
}

void expectUsableDeviceFeatures(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceFeatures2(scenario.usableDevice, NotNull()))
        .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
            auto* vulkan12 = static_cast<VkPhysicalDeviceVulkan12Features*>(features->pNext);
            auto* vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
            auto* maintenance = static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
            vulkan12->timelineSemaphore = vk::True;
            vulkan13->dynamicRendering = vk::True;
            vulkan13->synchronization2 = vk::True;
            maintenance->swapchainMaintenance1 = vk::True;
        });
}

void expectUsableQueueFamily(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceQueueFamilyProperties(scenario.usableDevice, NotNull(), _))
        .WillRepeatedly([](VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* families) {
            *count = 1;
            if (families != nullptr)
            {
                families[0] = VkQueueFamilyProperties {};
                families[0].queueFlags =
                    static_cast<VkQueueFlags>(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute);
                families[0].queueCount = 1;
            }
        });
}

void expectUsableSurfaceSupport(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceSupportKHR(scenario.usableDevice, 0, scenario.surface, NotNull()))
        .WillOnce([](VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32* supported) -> VkResult {
            *supported = vk::True;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectUsableSurfaceFormats(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceFormatsKHR(scenario.usableDevice, scenario.surface, NotNull(), _))
        .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkSurfaceFormatKHR* formats) {
            *count = 1;
            if (formats != nullptr)
            {
                formats[0] =
                    VkSurfaceFormatKHR {.format = static_cast<VkFormat>(vk::Format::eR8G8B8A8Unorm),
                                        .colorSpace = static_cast<VkColorSpaceKHR>(vk::ColorSpaceKHR::eSrgbNonlinear)};
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectUsablePresentModes(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceSurfacePresentModesKHR(scenario.usableDevice, scenario.surface, NotNull(), _))
        .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkPresentModeKHR* modes) {
            *count = 1;
            if (modes != nullptr)
            {
                modes[0] = static_cast<VkPresentModeKHR>(vk::PresentModeKHR::eFifo);
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectTwoDeviceScenario(panda::test::VulkanMock& vulkan, const DeviceScenario& scenario)
{
    expectPhysicalDevices(vulkan, scenario);
    expectDeviceProperties(vulkan, scenario);
    expectDeviceExtensions(vulkan, scenario);
    expectUsableDeviceFeatures(vulkan, scenario);
    expectUsableQueueFamily(vulkan, scenario);
    expectUsableSurfaceSupport(vulkan, scenario);
    expectUsableSurfaceFormats(vulkan, scenario);
    expectUsablePresentModes(vulkan, scenario);
}

void expectAvailableInstanceExtensions(panda::test::VulkanMock& vulkan,
                                       const std::vector<VkExtensionProperties>& available)
{
    EXPECT_CALL(vulkan, vkEnumerateInstanceExtensionProperties(nullptr, NotNull(), _))
        .WillRepeatedly([&available](const char*, std::uint32_t* count, VkExtensionProperties* properties) {
            *count = static_cast<std::uint32_t>(available.size());
            if (properties != nullptr)
            {
                std::ranges::copy(available, properties);
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectSingleDeviceQueryFailure(panda::test::VulkanMock& vulkan, VkPhysicalDevice device)
{
    EXPECT_CALL(vulkan, vkEnumeratePhysicalDevices(_, NotNull(), _))
        .WillRepeatedly([device](VkInstance, std::uint32_t* count, VkPhysicalDevice* devices) {
            *count = 1;
            if (devices != nullptr)
            {
                devices[0] = device;
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(device, NotNull()))
        .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            *properties = VkPhysicalDeviceProperties {};
            properties->apiVersion = vk::ApiVersion13;
            std::ranges::copy(std::string_view {"Broken GPU"}, std::span {properties->deviceName}.begin());
        });
    EXPECT_CALL(vulkan, vkEnumerateDeviceExtensionProperties(device, nullptr, NotNull(), _))
        .WillOnce([](VkPhysicalDevice, const char*, std::uint32_t* count, VkExtensionProperties*) {
            *count = 0;
            return static_cast<VkResult>(vk::Result::eErrorUnknown);
        });
}
}

TEST(DeviceSelection, PreservesWindowExtensionQueryFailure)
{
    auto surface = testing::StrictMock<panda::test::WindowSurfaceMock> {};
    EXPECT_CALL(surface, getRequiredInstanceExtensions())
        .WillOnce(Return(std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure,
                                                           "backend unavailable",
                                                           panda::NativeError {.api = "test window", .code = 17})}));
    const auto extensions = panda::detail::instanceExtensions(&surface);
    ASSERT_FALSE(extensions.has_value());
    EXPECT_EQ(extensions.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(extensions.error().native.has_value());
    EXPECT_EQ(extensions.error().native->api, "test window");
    EXPECT_EQ(extensions.error().native->code, 17);
}

TEST(DeviceSelection, RejectsUnavailableWindowExtension)
{
    auto vulkan = panda::test::ScopedMock<panda::test::VulkanMock> {};
    auto surface = testing::StrictMock<panda::test::WindowSurfaceMock> {};
    EXPECT_CALL(surface, getRequiredInstanceExtensions())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtensionResult {
            std::vector<std::string> {vk::KHRSurfaceExtensionName, "VK_EXT_window_missing"}
    }));
    EXPECT_CALL(vulkan, vkEnumerateInstanceExtensionProperties(nullptr, NotNull(), _))
        .WillRepeatedly([](const char*, std::uint32_t* count, VkExtensionProperties* properties) {
            *count = 1;
            if (properties != nullptr)
            {
                auto surfaceExtension = VkExtensionProperties {};
                std::ranges::copy(std::string_view {vk::KHRSurfaceExtensionName},
                                  std::span {surfaceExtension.extensionName}.begin());
                *properties = surfaceExtension;
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    const auto extensions = panda::detail::instanceExtensions(&surface);
    ASSERT_FALSE(extensions.has_value());
    EXPECT_EQ(extensions.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(extensions.error().message.find("VK_EXT_window_missing"), std::string::npos);
}

TEST(DeviceSelection, AddsAvailableMaintenanceExtensionsWithoutDuplicates)
{
    auto vulkan = panda::test::ScopedMock<panda::test::VulkanMock> {};
    auto surface = testing::StrictMock<panda::test::WindowSurfaceMock> {};
    EXPECT_CALL(surface, getRequiredInstanceExtensions())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtensionResult {
            std::vector<std::string> {vk::KHRSurfaceExtensionName, vk::KHRGetSurfaceCapabilities2ExtensionName}
    }));
    const auto available = std::vector {makeExtension(vk::KHRSurfaceExtensionName),
                                        makeExtension(vk::KHRGetSurfaceCapabilities2ExtensionName),
                                        makeExtension(vk::KHRSurfaceMaintenance1ExtensionName)};
    expectAvailableInstanceExtensions(vulkan, available);
    const auto extensions = panda::detail::instanceExtensions(&surface);
    ASSERT_TRUE(extensions.has_value());
    EXPECT_TRUE(extensions->hasKhrMaintenance);
    EXPECT_FALSE(extensions->hasExtMaintenance);
    EXPECT_EQ(extensions->names.size(), 3U);
    EXPECT_EQ(extensions->names.back(), vk::KHRSurfaceMaintenance1ExtensionName);
}

TEST(DeviceSelection, LogsAndSkipsDeviceInspectionFailureWhenAnotherDeviceIsUsable)
{
    auto vulkan = panda::test::ScopedMock<panda::test::VulkanMock> {};
    auto logRecords = panda::test::LogRecords {};
    auto logSink = panda::test::ProcessSinkRegistration {logRecords};
    const auto scenario = makeDeviceScenario();
    expectTwoDeviceScenario(vulkan, scenario);

    const auto selected = panda::detail::selectDevice(
        vk::Instance {scenario.instance},
        scenario.surface,
        panda::detail::InstanceExtensions {.names = {}, .hasKhrMaintenance = true, .hasExtMaintenance = false});

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->physicalDevice, vk::PhysicalDevice {scenario.usableDevice});
    EXPECT_EQ(selected->info.name, "Usable GPU");
    ASSERT_EQ(logRecords.entries.size(), 1U);
    EXPECT_EQ(logRecords.entries.front().level, panda::log::Level::Warning);
    EXPECT_NE(logRecords.entries.front().message.find("Broken GPU"), std::string::npos);
    EXPECT_NE(logRecords.entries.front().message.find("ErrorUnknown"), std::string::npos);
}

TEST(DeviceSelection, PreservesInspectionErrorWhenNoDeviceCanBeSelected)
{
    auto vulkan = panda::test::ScopedMock<panda::test::VulkanMock> {};
    const auto brokenDevice = fakeHandle<VkPhysicalDevice>(1);
    const auto instance = fakeHandle<VkInstance>(2);
    const auto surface = fakeHandle<VkSurfaceKHR>(3);
    expectSingleDeviceQueryFailure(vulkan, brokenDevice);

    const auto selected = panda::detail::selectDevice(
        vk::Instance {instance},
        surface,
        panda::detail::InstanceExtensions {.names = {}, .hasKhrMaintenance = true, .hasExtMaintenance = false});

    ASSERT_FALSE(selected.has_value());
    EXPECT_EQ(selected.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(selected.error().native.has_value());
    EXPECT_EQ(selected.error().native->code, std::to_underlying(vk::Result::eErrorUnknown));
}

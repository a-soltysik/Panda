#include "Device.h"

#include <ranges>

#include "utils/Assert.h"
#include "utils/format/api/vulkan/ResultFormatter.h"

namespace panda::gfx::vulkan
{

Device::Device(const vk::Instance& instance,
               const vk::SurfaceKHR& currentSurface,
               std::span<const char* const> requiredExtensions)
    : physicalDevice {pickPhysicalDevice(instance, currentSurface, requiredExtensions)},
      queueFamilies {findQueueFamilies(physicalDevice, currentSurface).value()},
      logicalDevice {createLogicalDevice(physicalDevice, queueFamilies, requiredExtensions)},
      surface {currentSurface}
{
}

Device::Device(const vk::Instance& instance,
               const vk::SurfaceKHR& currentSurface,
               std::span<const char* const> requiredExtensions,
               std::span<const char* const> requiredValidationLayers)
    : physicalDevice {pickPhysicalDevice(instance, currentSurface, requiredExtensions)},
      queueFamilies(findQueueFamilies(physicalDevice, currentSurface).value()),
      logicalDevice {createLogicalDevice(physicalDevice, queueFamilies, requiredExtensions, requiredValidationLayers)},
      surface {currentSurface}
{
}

auto Device::pickPhysicalDevice(const vk::Instance& instance,
                                const vk::SurfaceKHR& surface,
                                std::span<const char* const> requiredExtensions) -> vk::PhysicalDevice
{
    const auto devices =
        expect(instance.enumeratePhysicalDevices(), vk::Result::eSuccess, "Can't enumerate physical devices");

    const auto it = std::ranges::find_if(devices, [&surface, requiredExtensions](const auto& currentDevice) {
        return isDeviceSuitable(currentDevice, surface, requiredExtensions);
    });

    expectNot(it, devices.cend(), "None of physical devices is suitable");
    return *it;
}

auto Device::isDeviceSuitable(vk::PhysicalDevice device,
                              vk::SurfaceKHR surface,
                              std::span<const char* const> requiredExtensions) -> bool
{
    const auto queueFamilies = findQueueFamilies(device, surface);
    const auto swapChainSupport = querySwapChainSupport(device, surface);
    return queueFamilies && checkDeviceExtensionSupport(device, requiredExtensions) &&
           !swapChainSupport.formats.empty() && !swapChainSupport.presentationModes.empty();
}

auto Device::findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> std::optional<QueueFamilies>
{
    const auto queueFamilies = device.getQueueFamilyProperties();
    auto queueFamilyIndices = QueueFamilies {};

    auto isGraphicsSet = false;
    auto isPresentSet = false;

    for (auto i = size_t {}; i < queueFamilies.size(); i++)
    {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
        {
            log::Info("Graphics queue index: {}", i);
            queueFamilyIndices.graphicsFamily = i;
            isGraphicsSet = true;
        }
        if (device.getSurfaceSupportKHR(i, surface).value != 0)
        {
            log::Info("Presentation queue index: {}", i);
            queueFamilyIndices.presentationFamily = i;
            isPresentSet = true;
        }
        if (isGraphicsSet && isPresentSet)
        {
            return queueFamilyIndices;
        }
    }

    return {};
}

auto Device::querySwapChainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) -> SwapChainSupportDetails
{
    return {device.getSurfaceCapabilitiesKHR(surface).value,
            device.getSurfaceFormatsKHR(surface).value,
            device.getSurfacePresentModesKHR(surface).value};
}

auto Device::checkDeviceExtensionSupport(vk::PhysicalDevice device, std::span<const char* const> requiredExtensions)
    -> bool
{
    const auto availableExtensions = device.enumerateDeviceExtensionProperties();
    if (availableExtensions.result != vk::Result::eSuccess)
    {
        log::Error("Can't enumerate device extensions: {}", availableExtensions.result);
        return false;
    }

    for (const auto* extension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions.value,
            [extension](const auto& availableExtension) {
                return std::string_view {extension} == std::string_view {availableExtension};
            },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.value.cend())
        {
            log::Warning("{} extension is unavailable", extension);
            return false;
        }
    }
    return true;
}

auto Device::createLogicalDevice(vk::PhysicalDevice device,
                                 const QueueFamilies& queueFamilies,
                                 std::span<const char* const> requiredExtensions) -> vk::Device
{
    const auto uniqueFamilies = queueFamilies.getUniqueQueueFamilies();
    const auto queuePriority = 1.f;

    auto queueCreateInfos = std::vector<vk::DeviceQueueCreateInfo> {};
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (const auto queueFamily : queueFamilies.getUniqueQueueFamilies())
    {
        queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo({}, queueFamily, 1, &queuePriority));
    }
    const auto physicalDeviceFeatures = vk::PhysicalDeviceFeatures {};

    auto createInfo = vk::DeviceCreateInfo({}, queueCreateInfos, {}, requiredExtensions, &physicalDeviceFeatures);

    return expect(device.createDevice(createInfo), vk::Result::eSuccess, "Can't create physical device");
}

auto Device::createLogicalDevice(vk::PhysicalDevice device,
                                 const QueueFamilies& queueFamilies,
                                 std::span<const char* const> requiredExtensions,
                                 std::span<const char* const> requiredValidationLayers) -> vk::Device
{
    const auto uniqueFamilies = queueFamilies.getUniqueQueueFamilies();
    const auto queuePriority = 1.f;

    auto queueCreateInfos = std::vector<vk::DeviceQueueCreateInfo> {};
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (const auto queueFamily : queueFamilies.getUniqueQueueFamilies())
    {
        queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo({}, queueFamily, 1, &queuePriority));
    }
    const auto physicalDeviceFeatures = vk::PhysicalDeviceFeatures {};

    auto createInfo = vk::DeviceCreateInfo({},
                                           queueCreateInfos,
                                           requiredValidationLayers,
                                           requiredExtensions,
                                           &physicalDeviceFeatures);

    return expect(device.createDevice(createInfo), vk::Result::eSuccess, "Can't create physical device");
}

auto Device::querySwapChainSupport() -> SwapChainSupportDetails
{
    return querySwapChainSupport(physicalDevice, surface);
}

auto Device::QueueFamilies::getUniqueQueueFamilies() const -> std::unordered_set<uint32_t>
{
    return std::unordered_set<uint32_t> {graphicsFamily, presentationFamily};
}

}
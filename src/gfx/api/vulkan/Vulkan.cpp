VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>
#include <iostream>

namespace panda::gfx::vulkan
{

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

Vulkan::Vulkan()
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    instance = createInstance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    if constexpr (shouldEnableValidationLayers())
    {
        debugMessenger = instance.createDebugUtilsMessengerEXT(debugMessengerCreateInfo);
    }

    physicalDevice = pickPhysicalDevice();
    if (!physicalDevice)
    {
        throw std::runtime_error {"Unable to pick physical device"};
    }

    const auto queueFamily = findQueueFamily(physicalDevice);
    if (!queueFamily.index.has_value())
    {
        throw std::runtime_error {"Unable to create logical device"};
    }
    device = createLogicalDevice(queueFamily.index.value());
    graphicsQueue = device.getQueue(queueFamily.index.value(), 0);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
}

Vulkan::~Vulkan() noexcept
{
    device.destroy();

    if constexpr (shouldEnableValidationLayers())
    {
        instance.destroyDebugUtilsMessengerEXT(debugMessenger);
    }

    instance.destroy();
}

auto Vulkan::createInstance() -> vk::Instance
{
    const auto appInfo =
        vk::ApplicationInfo(PROJECT_NAME, VK_API_VERSION_1_0, TARGET_NAME, VK_API_VERSION_1_0, VK_API_VERSION_1_3);

    const auto requiredExtensions = getRequiredExtensions();

    if (!areRequiredExtensionsAvailable(requiredExtensions))
    {
        throw std::runtime_error {"There are missing extensions"};
    }

    auto createInfo =
        vk::InstanceCreateInfo({}, &appInfo, {}, {}, requiredExtensions.size(), requiredExtensions.data());

    if constexpr (shouldEnableValidationLayers())
    {
        if (!enableValidationLayers(createInfo))
        {
            std::cerr << "Unable to enable validation Layers\n";
        }

        createInfo.pNext = &debugMessengerCreateInfo;
    }
    return vk::createInstance(createInfo);
}

auto Vulkan::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.enabledLayerCount = requiredValidationLayers.size();
        createInfo.ppEnabledLayerNames = requiredValidationLayers.data();

        return true;
    }
    return false;
}

auto Vulkan::getRequiredExtensions() -> std::vector<const char*>
{
    auto glfwExtensionsCount = uint32_t {};
    const auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    auto extensions = std::vector(glfwExtensions, glfwExtensions + glfwExtensionsCount);

    if constexpr (shouldEnableValidationLayers())
    {

        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    return extensions;
}

auto Vulkan::areRequiredExtensionsAvailable(const std::vector<const char*>& requiredExtensions) -> bool
{
    const auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    for (const auto* requiredExtension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions,
            [requiredExtension](const auto& availableExtension)
            { return std::string_view {requiredExtension} == std::string_view {availableExtension}; },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.cend())
        {
            std::cerr << requiredExtension << " extension is unavailable\n";
            return false;
        }
    }
    return true;
}

auto Vulkan::createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT
{
    static constexpr auto severityMask =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

    static constexpr auto typeMask =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    return {{}, severityMask, typeMask, &debugCallback};
}

auto Vulkan::pickPhysicalDevice() const -> vk::PhysicalDevice
{
    const auto devices = instance.enumeratePhysicalDevices();
    const auto it = std::ranges::find_if(devices, isDeviceSuitable);

    if (it != devices.cend())
    {
        return *it;
    }
    return VK_NULL_HANDLE;
}

auto Vulkan::isDeviceSuitable(vk::PhysicalDevice device) -> bool
{
    return findQueueFamily(device).index.has_value();
}

auto Vulkan::findQueueFamily(vk::PhysicalDevice device) -> QueueFamily
{
    const auto queueFamilies = device.getQueueFamilyProperties();

    const auto it =
        std::ranges::find_if(queueFamilies, [](const auto& queueFamily) noexcept
                             { return static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics); });

    if (it == queueFamilies.cend())
    {
        return {};
    }
    return {std::distance(queueFamilies.begin(), it)};
}

auto Vulkan::areValidationLayersSupported() const -> bool
{
    const auto availableLayers = vk::enumerateInstanceLayerProperties();

    for (const auto* layerName : requiredValidationLayers)
    {
        const auto it = std::ranges::find_if(
            availableLayers,
            [layerName](const auto& availableLayer)
            { return std::string_view {layerName} == std::string_view {availableLayer}; },
            &vk::LayerProperties::layerName);

        if (it == availableLayers.cend())
        {
            return false;
        }
    }

    return true;
}

auto Vulkan::createLogicalDevice(uint32_t queueFamilyIndex) const -> vk::Device
{
    const auto queuePriority = 1.f;
    const auto queueCreateInfo = vk::DeviceQueueCreateInfo({}, queueFamilyIndex, 1, &queuePriority);
    const auto physicalDeviceFeatures = vk::PhysicalDeviceFeatures {};

    if constexpr (shouldEnableValidationLayers())
    {
        const auto createInfo = vk::DeviceCreateInfo({}, vk::ArrayProxyNoTemporaries(queueCreateInfo),
                                                     requiredValidationLayers, {}, &physicalDeviceFeatures);
        return physicalDevice.createDevice(createInfo);
    }
    else
    {
        const auto createInfo =
            vk::DeviceCreateInfo({}, vk::ArrayProxyNoTemporaries(queueCreateInfo), {}, {}, &physicalDeviceFeatures);
        return physicalDevice.createDevice(createInfo);
    }
}

auto Vulkan::render() -> void { }

}

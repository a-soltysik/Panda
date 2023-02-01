#include "Vulkan.h"

#include <algorithm>
#include <bit>
#include <iostream>
#include <iterator>

#include "ValidationLayersHandler.h"

#define GET_PROCEDURE(instance, name) std::bit_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));

namespace panda::gfx::vk
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

auto Vulkan::createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT& createInfo) -> VkResult
{
    const auto function = GET_PROCEDURE(instance, vkCreateDebugUtilsMessengerEXT);
    if (function != nullptr)
    {
        return function(instance, &createInfo, nullptr, &debugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

auto Vulkan::destroyDebugMessenger() const -> void
{
    const auto function = GET_PROCEDURE(instance, vkDestroyDebugUtilsMessengerEXT);
    if (function != nullptr)
    {
        function(instance, debugMessenger, nullptr);
    }
}

Vulkan::~Vulkan() noexcept
{
    destroy();
}

auto Vulkan::destroy() -> void
{
    if (isInitialized)
    {
        if constexpr (shouldEnableValidationLayers())
        {
            destroyDebugMessenger();
        }
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        isInitialized = false;
    }
}

auto Vulkan::cleanup() -> void
{
    destroy();
}

auto Vulkan::init() -> bool
{
    if (!createInstance())
    {
        return false;
    }
    isInitialized = true;
    if (!setupDebugMessenger())
    {
        std::cerr << "Unable to setup debug messenger\n";
    }

    physicalDevice = pickPhysicalDevice();
    if (physicalDevice == VK_NULL_HANDLE)
    {
        std::cerr << "Unable to pick physical device\n";
        return false;
    }
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "Current GPU: " << deviceProperties.deviceName << "\n";

    createLogicalDevice();

    return true;
}

auto Vulkan::createInstance() -> bool
{
    auto appInfo = VkApplicationInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = PROJECT_NAME;
    appInfo.applicationVersion = VK_API_VERSION_1_0;
    appInfo.pEngineName = PROJECT_NAME;
    appInfo.engineVersion = VK_API_VERSION_1_0;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto createInfo = VkInstanceCreateInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    const auto requiredExtensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = requiredExtensions.size();
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    checkRequiredExtensions(requiredExtensions);

    if constexpr (shouldEnableValidationLayers())
    {
        if (!enableValidationLayers(createInfo))
        {
            std::cerr << "Unable to enable validation Layers\n";
        }

        createInfo.pNext = &debugMessengerCreateInfo;
    }

    return vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS;
}

auto Vulkan::setupDebugMessenger() -> bool
{
    if constexpr (!shouldEnableValidationLayers())
    {
        return true;
    }

    return createDebugUtilsMessengerExt(debugMessengerCreateInfo) == VK_SUCCESS;
}

auto Vulkan::enableValidationLayers(VkInstanceCreateInfo& createInfo) -> bool
{
    validationLayersHandler = ValidationLayersHandler {};
    validationLayersHandler.add("VK_LAYER_KHRONOS_validation");

    if (validationLayersHandler.areValidationLayersSupported())
    {
        createInfo.enabledLayerCount = validationLayersHandler.getCount();
        createInfo.ppEnabledLayerNames = validationLayersHandler.getData();

        return true;
    }
    return false;
}

auto Vulkan::getRequiredExtensions() -> std::vector<const char*>
{
    auto glfwExtensionsCount = uint32_t {};
    const auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    auto extensions = std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionsCount);

    if constexpr (shouldEnableValidationLayers())
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    return extensions;
}

auto Vulkan::getAvailableExtensions() -> std::vector<VkExtensionProperties>
{
    auto extensionCount = uint32_t {};
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    auto availableExtensions = std::vector<VkExtensionProperties>(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    return availableExtensions;
}

auto Vulkan::checkRequiredExtensions(const std::vector<const char*>& requiredExtensions) -> void
{
    const auto availableExtensions = getAvailableExtensions();
    for (const auto* requiredExtension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions,
            [requiredExtension](const auto* availableExtension)
            { return std::string_view {requiredExtension} == std::string_view {availableExtension}; },
            &VkExtensionProperties::extensionName);

        if (it == availableExtensions.cend())
        {
            std::cerr << requiredExtension << " extension is unavailable\n";
        }
    }
}

auto Vulkan::createDebugMessengerCreateInfo() noexcept -> VkDebugUtilsMessengerCreateInfoEXT
{
    auto createInfo = VkDebugUtilsMessengerCreateInfoEXT {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    return createInfo;
}

auto Vulkan::pickPhysicalDevice() const -> VkPhysicalDevice
{
    auto deviceCount = uint32_t {};
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        return VK_NULL_HANDLE;
    }
    auto devices = std::vector<VkPhysicalDevice>(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    const auto it = std::ranges::find_if(devices, isDeviceSuitable);

    if (it != devices.cend())
    {
        return *it;
    }
    return VK_NULL_HANDLE;
}

auto Vulkan::isDeviceSuitable(VkPhysicalDevice device) -> bool
{
    return findQueueFamily(device).index.has_value();
}

auto Vulkan::findQueueFamily(VkPhysicalDevice device) -> QueueFamily
{
    auto queueFamilyCount = uint32_t {};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    auto queueFamilies = std::vector<VkQueueFamilyProperties>(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    const auto it = std::ranges::find_if(
        queueFamilies, [](const auto& queueFamily) { return queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT; });

    if (it == queueFamilies.cend())
    {
        return {};
    }
    return {std::distance(queueFamilies.begin(), it)};
}

auto Vulkan::createLogicalDevice() -> bool
{
    const auto indices = findQueueFamily(physicalDevice);

    const auto queuePriority = 1.f;
    auto queueCreateInfo = VkDeviceQueueCreateInfo {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.index.value();
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    auto physicalDeviceFeatures = VkPhysicalDeviceFeatures{};

    auto createInfo = VkDeviceCreateInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &physicalDeviceFeatures;

    createInfo.enabledExtensionCount = 0;
    if constexpr (shouldEnableValidationLayers())
    {
        createInfo.enabledLayerCount = validationLayersHandler.getCount();
        createInfo.ppEnabledLayerNames = validationLayersHandler.getData();
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
    {
        return false;
    }
    vkGetDeviceQueue(device, indices.index.value(), 0, &graphicsQueue);
    return true;
}

}

#include "Vulkan.h"

#include <algorithm>
#include <bit>
#include <iostream>

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

auto Vulkan::createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT& createInfo,
                                          VkDebugUtilsMessengerEXT& debugMessenger) const -> VkResult
{
    const auto function = GET_PROCEDURE(instance, vkCreateDebugUtilsMessengerEXT);
    if (function != nullptr)
    {
        return function(instance, &createInfo, nullptr, &debugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

auto Vulkan::destroyDebugMessenger(const VkDebugUtilsMessengerEXT& debugMessenger) const -> void
{
    const auto func = GET_PROCEDURE(instance, vkDestroyDebugUtilsMessengerEXT);
    if (func != nullptr)
    {
        func(instance, debugMessenger, nullptr);
    }
}

Vulkan::~Vulkan() noexcept
{
    destroy();
}

auto Vulkan::destroy() const -> void
{
    if (isInitialized)
    {
        if constexpr (shouldEnableValidationLayers())
        {
            destroyDebugMessenger(debugMessenger);
        }
        vkDestroyInstance(instance, nullptr);
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
    appInfo.apiVersion = VK_API_VERSION_1_0;

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

    return createDebugUtilsMessengerExt(debugMessengerCreateInfo, debugMessenger) == VK_SUCCESS;
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

auto Vulkan::createDebugMessengerCreateInfo() -> VkDebugUtilsMessengerCreateInfoEXT
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

}

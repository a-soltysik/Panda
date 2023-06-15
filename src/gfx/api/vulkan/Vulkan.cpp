VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>

#include "utils/format/api/vulkan/ResultFormatter.h"

namespace panda::gfx::vulkan
{

namespace
{

VKAPI_ATTR auto VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                         [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                         [[maybe_unused]] void* pUserData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        log::Debug(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        log::Info(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        log::Warning(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        log::Error(pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
        log::Debug(pCallbackData->pMessage);
        break;
    }

    return VK_FALSE;
}

}

Vulkan::Vulkan(const Window& mainWindow)
    : instance {createInstance()}
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    if constexpr (shouldEnableValidationLayers())
    {
        debugMessenger = expect(instance->createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                vk::Result::eSuccess,
                                "Unable to create debug messenger");
        log::Info("Debug messenger is created");
    }
    surface = createSurface(mainWindow);
    log::Info("Created surface successfully");

    if constexpr (shouldEnableValidationLayers())
    {
        device = std::make_unique<Device>(*instance, surface, requiredDeviceExtensions, requiredValidationLayers);
    }
    else
    {
        device = std::make_unique<Device>(*instance, surface, requiredDeviceExtensions);
    }
    log::Info("Created device successfully");
    log::Info("Chosen GPU: {}", std::string_view {device->physicalDevice.getProperties().deviceName});

    VULKAN_HPP_DEFAULT_DISPATCHER.init(device->logicalDevice);

    renderer = std::make_unique<Renderer>(mainWindow, *device, surface);

    model = std::make_unique<Model>(*device,
                                    std::vector<Vertex> {
                                        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                        {{0.5f, -0.5f},  {0.0f, 1.0f, 0.0f}},
                                        {{0.5f, 0.5f},   {0.0f, 0.0f, 1.0f}},
                                        {{-0.5f, 0.5f},  {1.0f, 1.0f, 1.0f}}
    },
                                    std::vector<uint16_t> {0, 1, 2, 2, 3, 0});

    auto object = Object::createObject();
    object.mesh = model.get();
    object.color = {0.1f, 0.8f, 0.1f};
    object.transform.translation.x = 0.2f;

    objects.push_back(std::move(object));

    log::Info("Create new object \"rectangle\"");

    renderSystem = std::make_unique<RenderSystem>(*device, renderer->getSwapChainRenderPass());

    log::Info("Vulkan API has been successfully initialized");
}

Vulkan::~Vulkan() noexcept
{
    log::Info("Starting closing Vulkan API");

    shouldBe(device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    if constexpr (shouldEnableValidationLayers())
    {
        instance->destroyDebugUtilsMessengerEXT(debugMessenger);
    }
}

auto Vulkan::createInstance() -> std::unique_ptr<vk::Instance, InstanceDeleter>
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    const auto appInfo = vk::ApplicationInfo(PROJECT_NAME,
                                             VK_API_VERSION_1_0,
                                             ENGINE_TARGET_NAME,
                                             VK_API_VERSION_1_0,
                                             VK_API_VERSION_1_3);

    const auto requiredExtensions = getRequiredExtensions();

    expect(areRequiredExtensionsAvailable(requiredExtensions), true, "There are missing extensions");

    auto createInfo =
        vk::InstanceCreateInfo({}, &appInfo, {}, {}, requiredExtensions.size(), requiredExtensions.data());

    if constexpr (shouldEnableValidationLayers())
    {
        shouldBe(enableValidationLayers(createInfo), true, "Unable to enable validation layers");
        createInfo.pNext = &debugMessengerCreateInfo;
    }

    return std::unique_ptr<vk::Instance, InstanceDeleter> {
        new vk::Instance {
            expect(vk::createInstance(createInfo), vk::Result::eSuccess, "Creating instance didn't succeed")},
        InstanceDeleter {surface}};
}

auto Vulkan::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.setPEnabledLayerNames(requiredValidationLayers);

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

auto Vulkan::areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool
{
    const auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    if (availableExtensions.result != vk::Result::eSuccess)
    {
        log::Error("Can't get available extensions: {}", availableExtensions.result);
        return false;
    }
    for (const auto* requiredExtension : requiredExtensions)
    {
        const auto it = std::ranges::find_if(
            availableExtensions.value,
            [requiredExtension](const auto& availableExtension) {
                return std::string_view {requiredExtension} == std::string_view {availableExtension};
            },
            &vk::ExtensionProperties::extensionName);

        if (it == availableExtensions.value.cend())
        {
            log::Error("{} extension is unavailable", requiredExtension);
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

auto Vulkan::areValidationLayersSupported() const -> bool
{
    const auto availableLayers = vk::enumerateInstanceLayerProperties();
    if (availableLayers.result != vk::Result::eSuccess)
    {
        log::Error("Can't enumerate available layers");
        return false;
    }

    for (const auto* layerName : requiredValidationLayers)
    {
        const auto it = std::ranges::find_if(
            availableLayers.value,
            [layerName](const auto& availableLayer) {
                return std::string_view {layerName} == std::string_view {availableLayer};
            },
            &vk::LayerProperties::layerName);

        if (it == availableLayers.value.cend())
        {
            log::Warning("{} layer is not supported", layerName);
            return false;
        }
    }

    return true;
}

auto Vulkan::makeFrame() -> void
{
    const auto commandBuffer = renderer->beginFrame();
    if (!commandBuffer)
    {
        return;
    }
    renderer->beginSwapChainRenderPass();
    renderSystem->render(commandBuffer, objects);
    renderer->endSwapChainRenderPass();
    renderer->endFrame();
}

auto Vulkan::createSurface(const Window& window) -> vk::SurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window.getHandle(), nullptr, &newSurface);

    return expect(
        newSurface,
        [](const auto* result) {
            return result != nullptr;
        },
        "Unable to create surface");
}

auto Vulkan::InstanceDeleter::operator()(vk::Instance* instance) const noexcept -> void
{
    log::Info("Destroying instance");
    instance->destroy(surface);
    instance->destroy();
}
}

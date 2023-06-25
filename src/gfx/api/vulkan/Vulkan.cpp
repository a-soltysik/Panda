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

auto createCubeModel(Device& device) -> std::unique_ptr<Model>
{
    std::vector<Vertex> vertices {

  // bottom face (white)
        {{-1.f, 1.f, -1.f},  {.9f, .9f, .9f}},
        {{-1.f, 1.f, 1.f},   {.9f, .9f, .9f}},
        {{1.f, 1.f, 1.f},    {.9f, .9f, .9f}},
        {{1.f, 1.f, -1.f},   {.9f, .9f, .9f}},

 // top face (yellow)
        {{-1.f, -1.f, -1.f}, {.8f, .8f, .1f}},
        {{1.f, -1.f, -1.f},  {.8f, .8f, .1f}},
        {{1.f, -1.f, 1.f},   {.8f, .8f, .1f}},
        {{-1.f, -1.f, 1.f},  {.8f, .8f, .1f}},

 // left face (orange)
        {{-1.f, -1.f, -1.f}, {.9f, .6f, .1f}},
        {{-1.f, -1.f, 1.f},  {.9f, .6f, .1f}},
        {{-1.f, 1.f, 1.f},   {.9f, .6f, .1f}},
        {{-1.f, 1.f, -1.f},  {.9f, .6f, .1f}},

 // right face (red)
        {{1.f, 1.f, -1.f},   {.8f, .1f, .1f}},
        {{1.f, 1.f, 1.f},    {.8f, .1f, .1f}},
        {{1.f, -1.f, 1.f},   {.8f, .1f, .1f}},
        {{1.f, -1.f, -1.f},  {.8f, .1f, .1f}},

 // front face (blue)
        {{-1.f, 1.f, 1.f},   {.1f, .1f, .8f}},
        {{1.f, 1.f, 1.f},    {.1f, .1f, .8f}},
        {{-1.f, -1.f, 1.f},  {.1f, .1f, .8f}},
        {{1.f, -1.f, 1.f},   {.1f, .1f, .8f}},

 // back face (green)
        {{-1.f, -1.f, -1.f}, {.1f, .8f, .1f}},
        {{1.f, 1.f, -1.f},   {.1f, .8f, .1f}},
        {{1.f, -1.f, -1.f},  {.1f, .8f, .1f}},
        {{-1.f, 1.f, -1.f},  {.1f, .8f, .1f}},
    };

    return std::make_unique<Model>(device, vertices, std::array<uint16_t, 36> {0,  1,  2,  0,  2,  3,  4,  5,  6,
                                                                               4,  6,  7,  8,  9,  10, 10, 11, 8,
                                                                               12, 13, 14, 14, 15, 12, 16, 18, 17,
                                                                               18, 19, 17, 20, 21, 22, 20, 23, 21});
}

}

Vulkan::Vulkan(const Window& window)
    : _instance {createInstance()}
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_instance);

    if constexpr (shouldEnableValidationLayers())
    {
        _debugMessenger = expect(_instance->createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                 vk::Result::eSuccess,
                                 "Unable to create debug messenger");
        log::Info("Debug messenger is created");
    }
    _surface = createSurface(window);
    log::Info("Created surface successfully");

    if constexpr (shouldEnableValidationLayers())
    {
        _device = std::make_unique<Device>(*_instance, _surface, requiredDeviceExtensions, _requiredValidationLayers);
    }
    else
    {
        _device = std::make_unique<Device>(*_instance, _surface, requiredDeviceExtensions);
    }
    log::Info("Created device successfully");
    log::Info("Chosen GPU: {}", std::string_view {_device->physicalDevice.getProperties().deviceName});

    VULKAN_HPP_DEFAULT_DISPATCHER.init(_device->logicalDevice);

    _renderer = std::make_unique<Renderer>(window, *_device, _surface);

    _model = createCubeModel(*_device);

    auto object = Object::createObject();
    object.mesh = _model.get();
    object.transform.rotation = {};
    object.transform.translation = {0.f, 0.f, 2.5f};
    object.transform.scale = {0.25f, 0.25f, 0.25f};

    _objects.push_back(std::move(object));

    log::Info("Create new object \"rectangle\"");

    _renderSystem = std::make_unique<RenderSystem>(*_device, _renderer->getSwapChainRenderPass());

    log::Info("Vulkan API has been successfully initialized");
}

Vulkan::~Vulkan() noexcept
{
    log::Info("Starting closing Vulkan API");

    shouldBe(_device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    if constexpr (shouldEnableValidationLayers())
    {
        _instance->destroyDebugUtilsMessengerEXT(_debugMessenger);
    }
}

auto Vulkan::createInstance() -> std::unique_ptr<vk::Instance, InstanceDeleter>
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    const auto appInfo = vk::ApplicationInfo(config::projectName.data(),
                                             VK_API_VERSION_1_0,
                                             config::targetName.data(),
                                             VK_API_VERSION_1_0,
                                             VK_API_VERSION_1_3);

    const auto requiredExtensions = getRequiredExtensions();

    expect(areRequiredExtensionsAvailable(requiredExtensions), true, "There are missing extensions");

    auto createInfo = vk::InstanceCreateInfo({},
                                             &appInfo,
                                             {},
                                             {},
                                             static_cast<uint32_t>(requiredExtensions.size()),
                                             requiredExtensions.data());

    if constexpr (shouldEnableValidationLayers())
    {
        shouldBe(enableValidationLayers(createInfo), true, "Unable to enable validation layers");
        createInfo.pNext = &debugMessengerCreateInfo;
    }

    return std::unique_ptr<vk::Instance, InstanceDeleter> {
        new vk::Instance {
            expect(vk::createInstance(createInfo), vk::Result::eSuccess, "Creating instance didn't succeed")},
        InstanceDeleter {_surface}};
}

auto Vulkan::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    _requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.setPEnabledLayerNames(_requiredValidationLayers);

        return true;
    }
    return false;
}

auto Vulkan::getRequiredExtensions() -> std::vector<const char*>
{
    auto glfwExtensionsCount = uint32_t {};
    const auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    if (glfwExtensions == nullptr)
    {
        return {};
    }

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

    for (const auto* layerName : _requiredValidationLayers)
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
    _camera.setPerspectiveProjection(glm::radians(50.f), _renderer->getAspectRatio(), 0.1f, 10.f);

    const auto commandBuffer = _renderer->beginFrame();
    if (!commandBuffer)
    {
        return;
    }
    _renderer->beginSwapChainRenderPass();
    _renderSystem->render(commandBuffer, _objects, _camera);
    _renderer->endSwapChainRenderPass();
    _renderer->endFrame();
}

auto Vulkan::createSurface(const Window& window) -> vk::SurfaceKHR
{
    auto* newSurface = VkSurfaceKHR {};
    glfwCreateWindowSurface(static_cast<VkInstance>(*_instance), window.getHandle(), nullptr, &newSurface);

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

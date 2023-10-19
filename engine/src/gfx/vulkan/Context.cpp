#include "panda/Common.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>

#include "panda/gfx/vulkan/Context.h"
#include "panda/gfx/vulkan/UboLight.h"
#include "panda/utils/format/gfx/api/vulkan/ResultFormatter.h"

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

Context::Context(const Window& window)
    : _instance {createInstance(window)},
      _window {window}
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*_instance);

    if constexpr (shouldEnableValidationLayers())
    {
        _debugMessenger = expect(_instance->createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                 vk::Result::eSuccess,
                                 "Unable to create debug messenger");
        log::Info("Debug messenger is created");
    }
    _surface = _window.createSurface(*_instance);
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

    _uboFragBuffers.reserve(maxFramesInFlight);
    _uboVertBuffers.reserve(maxFramesInFlight);

    for (auto i = uint32_t {}; i < maxFramesInFlight; i++)
    {
        _uboFragBuffers.push_back(std::make_unique<Buffer>(
            *_device,
            sizeof(FragUbo),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            _device->physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment));
        _uboFragBuffers.back()->mapWhole();

        _uboVertBuffers.push_back(std::make_unique<Buffer>(
            *_device,
            sizeof(VertUbo),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            _device->physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment));
        _uboVertBuffers.back()->mapWhole();
    }

    _globalPool = DescriptorPool::Builder(*_device)
                      .addPoolSize(vk::DescriptorType::eUniformBuffer, maxFramesInFlight)
                      .build(maxFramesInFlight);
    _globalSetLayout = DescriptorSetLayout::Builder(*_device)
                           .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex)
                           .addBinding(1, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment)
                           .build();

    for (auto i = uint32_t {}; i < maxFramesInFlight; i++)
    {
        DescriptorWriter(*_device, *_globalSetLayout, *_globalPool)
            .writeBuffer(0, _uboVertBuffers[i]->getDescriptorInfo())
            .writeBuffer(1, _uboFragBuffers[i]->getDescriptorInfo())
            .build(_globalDescriptorSets[i]);
    }

    _renderSystem = std::make_unique<RenderSystem>(*_device,
                                                   _renderer->getSwapChainRenderPass(),
                                                   _globalSetLayout->getDescriptorSetLayout());

    _pointLightSystem = std::make_unique<PointLightSystem>(*_device,
                                                           _renderer->getSwapChainRenderPass(),
                                                           _globalSetLayout->getDescriptorSetLayout());

    log::Info("Vulkan API has been successfully initialized");
}

Context::~Context() noexcept
{
    log::Info("Starting closing Vulkan API");

    shouldBe(_device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    if constexpr (shouldEnableValidationLayers())
    {
        _instance->destroyDebugUtilsMessengerEXT(_debugMessenger);
    }
}

auto Context::createInstance(const Window& window) -> std::unique_ptr<vk::Instance, InstanceDeleter>
{
    const auto dynamicLoader = vk::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    const auto appInfo = vk::ApplicationInfo(config::projectName.data(),
                                             VK_API_VERSION_1_0,
                                             config::engineName.data(),
                                             VK_API_VERSION_1_0,
                                             VK_API_VERSION_1_3);

    const auto requiredExtensions = getRequiredExtensions(window);

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

auto Context::enableValidationLayers(vk::InstanceCreateInfo& createInfo) -> bool
{
    _requiredValidationLayers.push_back("VK_LAYER_KHRONOS_validation");

    if (areValidationLayersSupported())
    {
        createInfo.setPEnabledLayerNames(_requiredValidationLayers);

        return true;
    }
    return false;
}

auto Context::getRequiredExtensions(const Window& window) -> std::vector<const char*>
{
    auto extensions = window.getRequiredExtensions();

    if constexpr (shouldEnableValidationLayers())
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    return extensions;
}

auto Context::areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool
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

auto Context::createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT
{
    static constexpr auto severityMask =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

    static constexpr auto typeMask =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    return {{}, severityMask, typeMask, &debugCallback};
}

auto Context::areValidationLayersSupported() const -> bool
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

auto Context::makeFrame(float deltaTime, const Camera& camera, std::span<const Object> objects, const Lights& lights)
    -> void
{
    const auto commandBuffer = _renderer->beginFrame();
    if (!commandBuffer)
    {
        return;
    }
    const auto frameIndex = _renderer->getFrameIndex();
    const auto frameInfo = FrameInfo {.camera = camera,
                                      .commandBuffer = commandBuffer,
                                      .descriptorSet = _globalDescriptorSets[frameIndex],
                                      .frameIndex = frameIndex,
                                      .deltaTime = deltaTime};

    auto vertUbo = VertUbo {
        camera.getProjection(),
        camera.getView(),
    };

    auto fragUbo = FragUbo {
        camera.getInverseView(),
        {},
        {},
        {},
        {0.1f, 0.1f, 0.1f},
        {},
        {}
    };

    _pointLightSystem->update(lights, fragUbo);
    _uboVertBuffers[frameIndex]->writeAt(vertUbo, 0);
    _uboFragBuffers[frameIndex]->writeAt(fragUbo, 0);
    _renderer->beginSwapChainRenderPass();
    _renderSystem->render(frameInfo, objects);

    _pointLightSystem->render(frameInfo, lights);
    _renderer->endSwapChainRenderPass();
    _renderer->endFrame();
}

auto Context::getDevice() const noexcept -> const Device&
{
    return *_device;
}

auto Context::getRenderer() const noexcept -> const Renderer&
{
    return *_renderer;
}

auto Context::InstanceDeleter::operator()(vk::Instance* instance) const noexcept -> void
{
    log::Info("Destroying instance");
    instance->destroy(surface);
    instance->destroy();

    delete instance;
}
}

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Vulkan.h"

#include <algorithm>

#include "UboLight.h"
#include "app/inputHandlers/MouseHandler.h"
#include "app/movementHandlers/MovementHandler.h"
#include "app/movementHandlers/RotationHandler.h"
#include "utils/format/gfx/api/vulkan/ResultFormatter.h"

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

Vulkan::Vulkan(const Window& window)
    : _instance {createInstance()},
      _cameraObject {Object::createObject()},
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

    _model = Model::loadObj(*_device, config::resource::models / "smooth_vase.obj");
    _floorModel = Model::loadObj(*_device, config::resource::models / "square.obj");

    auto object = Object::createObject();
    object.mesh = _model.get();
    object.transform.rotation = {};
    object.transform.translation = {1.f, 0.f, 0.f};
    object.transform.scale = {5.f, 5.f, 5.f};

    _objects.push_back(std::move(object));

    object = Object::createObject();
    object.mesh = _model.get();
    object.transform.rotation = {};
    object.transform.translation = {-1.f, 0.f, 0.f};
    object.transform.scale = {5.f, 5.f, 5.f};

    _objects.push_back(std::move(object));

    object = Object::createObject();
    object.mesh = _floorModel.get();
    object.transform.rotation = {};
    object.transform.translation = {0.f, 0.f, 0.f};
    object.transform.scale = {10.f, 10.f, 10.f};

    _objects.push_back(std::move(object));

    log::Info("Create new object \"rectangle\"");

    _lights.emplace_back(PointLight {
        {2.f, -2.f, -1.5f},
        {1.f, 0.5f, 1.f  },
        1.5f,
        0.1f
    });

    _lights.emplace_back(PointLight {
        {0.f, -2.f, 0.f},
        {1.f, 0.5f, 0.f},
        2.f,
        0.2f
    });

    _lights.emplace_back(PointLight {
        {-2.f, -2.f, -1.5f},
        {0.5f, 0.8f, 1.f  },
        1.5f,
        0.1f
    });

    _lights.emplace_back(DirectionalLight {
        {0.f, -2.f, 10.f},
        {1.f, .8f,  .8f },
        0.8f,
    });

    _cameraObject.transform.translation = {0.f, 2.f, -8.f};
    _camera.setViewYXZ(_cameraObject.transform.translation, _cameraObject.transform.rotation);

    _uboBuffers.reserve(maxFramesInFlight);

    for (auto i = uint32_t {}; i < maxFramesInFlight; i++)
    {
        _uboBuffers.push_back(std::make_unique<Buffer>(
            *_device,
            sizeof(GlobalUbo),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            _device->physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment));
        _uboBuffers.back()->mapWhole();
    }

    _globalPool = DescriptorPool::Builder(*_device)
                      .addPoolSize(vk::DescriptorType::eUniformBuffer, maxFramesInFlight)
                      .build(maxFramesInFlight);
    _globalSetLayout = DescriptorSetLayout::Builder(*_device)
                           .addBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eAllGraphics)
                           .build();

    for (auto i = uint32_t {}; i < maxFramesInFlight; i++)
    {
        DescriptorWriter(*_device, *_globalSetLayout, *_globalPool)
            .writeBuffer(0, _uboBuffers[i]->getDescriptorInfo())
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

    const auto extensionsSpan = std::span(glfwExtensions, glfwExtensionsCount);

    auto extensions = std::vector(extensionsSpan.begin(), extensionsSpan.end());

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

auto Vulkan::makeFrame(float deltaTime) -> void
{
    static constexpr auto rotationVelocity = 200.f;
    static constexpr auto moveVelocity = 2.5f;

    _camera.setPerspectiveProjection(glm::radians(50.f), _renderer->getAspectRatio(), 0.1f, 100.f);

    if (app::MouseHandler::instance(_window).getButtonState(GLFW_MOUSE_BUTTON_LEFT) ==
        app::MouseHandler::ButtonState::Pressed)
    {
        _cameraObject.transform.rotation +=
            glm::vec3 {app::RotationHandler::instance(_window).getRotation() * rotationVelocity * deltaTime, 0.f};
    }

    _cameraObject.transform.rotation.x =
        glm::clamp(_cameraObject.transform.rotation.x, -glm::half_pi<float>(), glm::half_pi<float>());
    _cameraObject.transform.rotation.y = glm::mod(_cameraObject.transform.rotation.y, glm::two_pi<float>());

    const auto rawMovement = app::MovementHandler::instance(_window).getMovement();

    const auto cameraDirection =
        glm::vec3 {glm::cos(-_cameraObject.transform.rotation.x) * glm::sin(_cameraObject.transform.rotation.y),
                   glm::sin(-_cameraObject.transform.rotation.x),
                   glm::cos(-_cameraObject.transform.rotation.x) * glm::cos(_cameraObject.transform.rotation.y)};
    const auto cameraRight =
        glm::vec3 {glm::cos(_cameraObject.transform.rotation.y), 0.f, -glm::sin(_cameraObject.transform.rotation.y)};

    auto translation = glm::vec3 {};
    if (rawMovement.z != 0.f)
    {
        translation += cameraDirection * rawMovement.z;
    }
    if (rawMovement.x != 0.f)
    {
        translation += cameraRight * rawMovement.x;
    }
    if (rawMovement.y != 0.f)
    {
        translation.y = rawMovement.y;
    }

    if (glm::dot(translation, translation) > std::numeric_limits<float>::epsilon())
    {
        _cameraObject.transform.translation += glm::normalize(translation) * moveVelocity * deltaTime;
    }

    _camera.setViewYXZ(_cameraObject.transform.translation,
                       {-_cameraObject.transform.rotation.x, _cameraObject.transform.rotation.y, 0.f});

    const auto commandBuffer = _renderer->beginFrame();
    if (!commandBuffer)
    {
        return;
    }
    const auto frameIndex = _renderer->getFrameIndex();
    const auto frameInfo = FrameInfo {.camera = _camera,
                                      .commandBuffer = commandBuffer,
                                      .descriptorSet = _globalDescriptorSets[frameIndex],
                                      .frameIndex = frameIndex,
                                      .deltaTime = deltaTime};

    auto it = std::ranges::find_if(_lights, [](const auto& light) {
        return std::get_if<DirectionalLight>(&light) != nullptr;
    });

    if (it != _lights.end())
    {
        auto& directionalLight = std::get<DirectionalLight>(*it);
        directionalLight.direction = glm::rotateY(directionalLight.direction, deltaTime);
    }

    auto ubo = GlobalUbo {
        _camera.getProjection(),
        _camera.getView(),
        _camera.getInverseView(),
        {0.1f, 0.1f, 0.1f, 1.f},
        {},
        {},
        {},
        {}
    };

    _pointLightSystem->update(_lights, ubo);
    _uboBuffers[frameIndex]->writeAt(ubo, 0);
    _renderer->beginSwapChainRenderPass();
    _renderSystem->render(frameInfo, _objects);

    _pointLightSystem->render(frameInfo, _lights);
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

    delete instance;
}
}

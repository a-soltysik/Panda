// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "drip/gfx/vulkan/core/Context.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <drip/common/log/LogMessageBuilder.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "drip/gfx/Config.hpp"
#include "drip/gfx/Window.hpp"
#include "drip/gfx/gui/GuiManager.hpp"
#include "drip/gfx/resource/Mesh.hpp"
#include "drip/gfx/resource/Texture.hpp"
#include "drip/gfx/scene/Camera.hpp"
#include "drip/gfx/scene/Scene.hpp"
#include "drip/gfx/utils/format/ResultFormatter.hpp"  // NOLINT(misc-include-cleaner)
#include "rendering/FrameInfo.hpp"
#include "rendering/UboLight.hpp"
#include "rendering/system/GuiRenderSystem.hpp"
#include "rendering/system/MeshRenderSystem.hpp"
#include "rendering/system/ParticlesRenderSystem.hpp"
#include "vulkan/core/Device.hpp"
#include "vulkan/memory/Buffer.hpp"
#include "vulkan/memory/Descriptor.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#ifdef WIN32
constexpr auto requiredDeviceExtensions = std::array {
    vk::KHRSwapchainExtensionName, vk::KHRPushDescriptorExtensionName, vk::KHRExternalMemoryWin32ExtensionName};
#else
constexpr auto requiredDeviceExtensions = std::array {
    vk::KHRSwapchainExtensionName, vk::KHRPushDescriptorExtensionName, vk::KHRExternalMemoryFdExtensionName};
#endif

namespace drip::gfx
{
namespace
{

VKAPI_ATTR auto VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                         [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT messageType,
                                         const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                         [[maybe_unused]] void* pUserData) -> vk::Bool32
{
    switch (messageSeverity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        common::log::Debug("{}", pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        common::log::Info("{}", pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        common::log::Warning("{}", pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        common::log::Error("{}", pCallbackData->pMessage);
        break;
    }

    return vk::False;
}

void imGuiCallback(VkResult result)
{
    common::ShouldBe(vk::Result {result}, vk::Result::eSuccess, "ImGui didn't succeed: {}", vk::Result {result});
}

auto updateLightUbo(const Scene& scene) -> FragUbo
{
    static constexpr auto ambientColor = 0.1F;

    auto fragUbo = FragUbo {
        .inverseView = scene.getCamera().getInverseView(),
        .pointLights = {},
        .directionalLights = {},
        .spotLights = {},
        .ambientColor = {ambientColor, ambientColor, ambientColor},
        .activePointLights = {},
        .activeDirectionalLights = {},
        .activeSpotLights = {}
    };
    for (auto i = size_t {}; i < fragUbo.directionalLights.size() && i < scene.getLights().directionalLights.size();
         i++)
    {
        fragUbo.directionalLights[i] = fromDirectionalLight(scene.getLights().directionalLights[i]);
    }
    for (auto i = size_t {}; i < fragUbo.pointLights.size() && i < scene.getLights().pointLights.size(); i++)
    {
        fragUbo.pointLights[i] = fromPointLight(scene.getLights().pointLights[i]);
    }
    for (auto i = size_t {}; i < fragUbo.spotLights.size() && i < scene.getLights().spotLights.size(); i++)
    {
        fragUbo.spotLights[i] = fromSpotLight(scene.getLights().spotLights[i]);
    }

    fragUbo.activeDirectionalLights = static_cast<uint32_t>(scene.getLights().directionalLights.size());
    fragUbo.activePointLights = static_cast<uint32_t>(scene.getLights().pointLights.size());
    fragUbo.activeSpotLights = static_cast<uint32_t>(scene.getLights().spotLights.size());

    return fragUbo;
}

auto updateCameraUbo(const Scene& scene) -> VertUbo
{
    return {.projection = scene.getCamera().getProjection(), .view = scene.getCamera().getView()};
}

}

Context::Context(const Window& window)
    : _instance {createInstance(window)},
      _debugMessenger {createDebugMessanger(*_instance)},
      _window {window}
{
    _surface = _window.createSurface(*_instance);
    common::log::Info("Created surface successfully");

    _device = std::make_unique<Device>(*_instance, _surface, requiredDeviceExtensions);

    common::log::Info("Created device successfully");
    common::log::Info("Chosen GPU: {}", std::string_view {_device->physicalDevice.getProperties().deviceName});

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

    common::log::Info("Vulkan API has been successfully initialized");

    initializeImGui();
    setupRenderSystems();
}

Context::~Context() noexcept
{
    common::log::Info("Starting closing Vulkan API");

    common::ShouldBe(_device->logicalDevice.waitIdle(), vk::Result::eSuccess, "Wait idle didn't succeed");

    ImGui_ImplVulkan_Shutdown();

    _debugMessenger.and_then([this](const auto& messenger) {
        _instance->destroyDebugUtilsMessengerEXT(messenger);
        return std::make_optional(messenger);
    });
}

auto Context::createInstance(const Window& window) -> std::unique_ptr<vk::Instance, InstanceDeleter>
{
    const auto dynamicLoader = vk::detail::DynamicLoader {};
    const auto vkGetInstanceProcAddr = dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    const auto projectName = std::string {config::projectName};
    const auto engineName = std::string {config::engineName};

    const auto appInfo = vk::ApplicationInfo {.pApplicationName = projectName.data(),
                                              .applicationVersion = vk::ApiVersion10,
                                              .pEngineName = engineName.data(),
                                              .engineVersion = vk::ApiVersion10,
                                              .apiVersion = vk::ApiVersion14};

    const auto requiredExtensions = getRequiredExtensions(window);

    common::Expect(areRequiredExtensionsAvailable(requiredExtensions), true, "There are missing extensions");

    auto createInfo = vk::InstanceCreateInfo {.pApplicationInfo = &appInfo,
                                              .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
                                              .ppEnabledExtensionNames = requiredExtensions.data()};

    if constexpr (shouldEnableValidationLayers())
    {
        common::ShouldBe(enableValidationLayers(createInfo), true, "Unable to enable validation layers");
        createInfo.pNext = &debugMessengerCreateInfo;
    }

    auto instance = std::unique_ptr<vk::Instance, InstanceDeleter> {
        new vk::Instance {
            common::Expect(vk::createInstance(createInfo), vk::Result::eSuccess, "Creating instance didn't succeed")
                .result()},
        InstanceDeleter {_surface}};
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    return instance;
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
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }
    extensions.push_back(vk::KHRPortabilityEnumerationExtensionName);

    return extensions;
}

auto Context::areRequiredExtensionsAvailable(std::span<const char* const> requiredExtensions) -> bool
{
    const auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    if (availableExtensions.result != vk::Result::eSuccess)
    {
        common::log::Error("Can't get available extensions: {}", availableExtensions.result);
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
            common::log::Error("{} extension is unavailable", requiredExtension);
            return false;
        }
    }
    return true;
}

auto Context::createDebugMessanger(vk::Instance instance) -> std::optional<vk::DebugUtilsMessengerEXT>
{
    if constexpr (shouldEnableValidationLayers())
    {
        const auto debugMessenger = common::Expect(instance.createDebugUtilsMessengerEXT(debugMessengerCreateInfo),
                                                   vk::Result::eSuccess,
                                                   "Unable to create debug messenger")
                                        .result();
        common::log::Info("Debug messenger is created");
        return debugMessenger;
    }
    else
    {
        return std::nullopt;
    }
}

auto Context::createDebugMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT
{
    static constexpr auto severityMask = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError      //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning  //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo     //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

    static constexpr auto typeMask = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral       //
                                     | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation  //
                                     | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    return {.messageSeverity = severityMask, .messageType = typeMask, .pfnUserCallback = debugCallback};
}

auto Context::areValidationLayersSupported() const -> bool
{
    const auto availableLayers = vk::enumerateInstanceLayerProperties();
    if (availableLayers.result != vk::Result::eSuccess)
    {
        common::log::Error("Can't enumerate available layers");
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
            common::log::Warning("{} layer is not supported", layerName);
            return false;
        }
    }

    return true;
}

void Context::makeFrame(const Scene& scene) const
{
    const auto commandBuffer = _renderer->beginFrame();
    if (!commandBuffer)
    {
        return;
    }

    const auto frameIndex = _renderer->getFrameIndex();

    _uboVertBuffers[frameIndex]->writeAt(updateCameraUbo(scene), 0);
    _uboFragBuffers[frameIndex]->writeAt(updateLightUbo(scene), 0);
    _renderer->beginSwapChainRenderPass();

    const auto frameInfo = FrameInfo {.scene = scene,
                                      .fragUbo = *_uboFragBuffers[frameIndex],
                                      .vertUbo = *_uboVertBuffers[frameIndex],
                                      .commandBuffer = commandBuffer,
                                      .frameIndex = frameIndex};
    for (const auto& renderSystem : _renderSystems)
    {
        renderSystem->render(frameInfo);
    }

    _renderer->endSwapChainRenderPass();
    _renderer->endFrame();
}

auto Context::getDevice() const noexcept -> const Device&
{
    return *_device;
}

void Context::initializeImGui()
{
    _guiPool = DescriptorPool::Builder(*_device)
                   .addPoolSize(vk::DescriptorType::eCombinedImageSampler, maxFramesInFlight)
                   .build(maxFramesInFlight, vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    auto initInfo = ImGui_ImplVulkan_InitInfo {
        .ApiVersion = vk::ApiVersion14,
        .Instance = *_instance,
        .PhysicalDevice = _device->physicalDevice,
        .Device = _device->logicalDevice,
        .QueueFamily = _device->queueFamilies.graphicsFamily,
        .Queue = _device->graphicsQueue,
        .DescriptorPool = _guiPool->getHandle(),
        .DescriptorPoolSize = {},
        .MinImageCount = maxFramesInFlight,
        .ImageCount = maxFramesInFlight,
        .PipelineCache = {},
        .PipelineInfoMain = {.RenderPass = _renderer->getSwapChainRenderPass(),
                               .Subpass = {},
                               .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
                               .PipelineRenderingCreateInfo = {}},
        .UseDynamicRendering = false,
        .Allocator = {},
        .CheckVkResultFn = imGuiCallback,
        .MinAllocationSize = {},
        .CustomShaderVertCreateInfo = {},
        .CustomShaderFragCreateInfo = {}
    };

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  //NOLINT(hicpp-signed-bitwise)

    ImGui_ImplVulkan_Init(&initInfo);
}

void Context::setupRenderSystems()
{
    _renderSystems.clear();
    _renderSystems.reserve(3);
    _renderSystems.push_back(std::make_unique<MeshRenderSystem>(*_device, *_renderer));
    _renderSystems.push_back(std::make_unique<ParticlesRenderSystem>(*_device, *_renderer));
    _renderSystems.push_back(std::make_unique<GuiRenderSystem>(_guiManager));
}

void Context::registerMesh(std::unique_ptr<Mesh> mesh)
{
    _meshes.push_back(std::move(mesh));
}

auto Context::getAspectRatio() const noexcept -> float
{
    return _renderer->getAspectRatio();
}

auto Context::getGuiManager() -> GuiManager&
{
    return _guiManager;
}

void Context::registerTexture(std::unique_ptr<Texture> texture)
{
    _textures.push_back(std::move(texture));
}

void Context::InstanceDeleter::operator()(vk::Instance* instance) const noexcept
{
    common::log::Info("Destroying instance");
    instance->destroy(surface);
    instance->destroy();

    delete instance;  //NOLINT(cppcoreguidelines-owning-memory)
}
}

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DeviceSelection.hpp"
#include "Presentation.hpp"
#include "VulkanError.hpp"

namespace panda
{
struct Context::InstanceServices
{
    [[nodiscard]] static auto create(const vk::raii::Context& loader,
                                     const WindowSurface* surface,
                                     const ContextOptions& options) -> Result<InstanceServices>;

    detail::InstanceExtensions extensions;
    vk::raii::Instance instance {nullptr};
    std::optional<vk::raii::DebugUtilsMessengerEXT> debugMessenger;
    std::optional<vk::raii::SurfaceKHR> surface;
};

namespace
{
auto validationLogLevel(vk::DebugUtilsMessageSeverityFlagBitsEXT severity) noexcept -> log::Level
{
    switch (severity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        return log::Level::Debug;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        return log::Level::Info;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        return log::Level::Warning;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        return log::Level::Error;
    }
    return log::Level::Warning;
}

VKAPI_ATTR auto VKAPI_CALL validationCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                              [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT type,
                                              const vk::DebugUtilsMessengerCallbackDataEXT* data,
                                              [[maybe_unused]] void* userData) noexcept -> vk::Bool32
{
    const auto* const message =
        data != nullptr && data->pMessage != nullptr ? data->pMessage : "Vulkan diagnostic without text";
    try
    {
        log::write(log::Logger::instance(), validationLogLevel(severity), "Vulkan validation: {}", message);
    }
    catch (...)
    {
        // Exceptions must not cross Vulkan's C callback boundary.
        return vk::False;
    }
    return vk::False;
}

auto validationMessengerCreateInfo() noexcept -> vk::DebugUtilsMessengerCreateInfoEXT
{
    static constexpr auto severityMask = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose    //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo     //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning  //
                                         | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    static constexpr auto typeMask = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral       //
                                     | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation  //
                                     | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    return {.messageSeverity = severityMask, .messageType = typeMask, .pfnUserCallback = validationCallback};
}

auto hasValidationLayer(const vk::raii::Context& loader) -> Result<bool>
{
    const auto listed =
        detail::enumerateVulkan<vk::LayerProperties>([&](std::uint32_t* count, vk::LayerProperties* output) {
            return vk::enumerateInstanceLayerProperties(count, output, *loader.getDispatcher());
        });
    if (!listed)
    {
        return std::unexpected {listed.error()};
    }
    return std::ranges::any_of(*listed, [](const auto& layer) {
        return std::string_view {layer.layerName.data()} == "VK_LAYER_KHRONOS_validation";
    });
}

auto verifyPrerequisites(const vk::raii::Context& loader, const ContextOptions& options) -> Result<void>
{
    const auto version = loader.enumerateInstanceVersion();
    if (version.result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(version.result)};
    }
    if (version.value < vk::ApiVersion13)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Vulkan loader does not support version 1.3")};
    }
    if (options.enableValidation)
    {
        const auto available = hasValidationLayer(loader);
        if (!available)
        {
            return std::unexpected {available.error()};
        }
        if (!*available)
        {
            return std::unexpected {makeError(ErrorCode::Unsupported, "VK_LAYER_KHRONOS_validation is unavailable")};
        }
    }
    return {};
}

auto createInstance(const vk::raii::Context& loader, const vk::InstanceCreateInfo& info) -> Result<vk::raii::Instance>
{
    auto instance = vk::Instance {};
    const auto result = vk::createInstance(&info, nullptr, &instance, *loader.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(result)};
    }
    // Unlike leaf objects, Instance construction uses the handle immediately
    // to load Vulkan functions, so check the result before creating the RAII wrapper.
    return vk::raii::Instance {loader, static_cast<VkInstance>(instance)};
}

auto makeInstance(const vk::raii::Context& loader,
                  const ContextOptions& options,
                  const detail::InstanceExtensions& extensions) -> Result<vk::raii::Instance>
{
    const auto extensionNames =
        extensions.names | std::views::transform(&std::string::c_str) | std::ranges::to<std::vector<const char*>>();
    static constexpr auto validationName = "VK_LAYER_KHRONOS_validation";
    const auto debugInfo = validationMessengerCreateInfo();
    const auto applicationInfo = vk::ApplicationInfo {.pApplicationName = options.applicationName.c_str(),
                                                      .pEngineName = "Panda",
                                                      .apiVersion = vk::ApiVersion13};
    const auto instanceInfo =
        vk::InstanceCreateInfo {.pNext = options.enableValidation ? &debugInfo : nullptr,
                                .pApplicationInfo = &applicationInfo,
                                .enabledLayerCount = options.enableValidation ? 1U : 0U,
                                .ppEnabledLayerNames = options.enableValidation ? &validationName : nullptr,
                                .enabledExtensionCount = static_cast<std::uint32_t>(extensionNames.size()),
                                .ppEnabledExtensionNames = extensionNames.data()};
    return createInstance(loader, instanceInfo);
}

auto makeDebugMessenger(const vk::raii::Instance& instance, bool enableValidation)
    -> Result<std::optional<vk::raii::DebugUtilsMessengerEXT>>
{
    if (!enableValidation)
    {
        return std::nullopt;
    }
    const auto* const dispatcher = instance.getDispatcher();
    if (dispatcher->vkCreateDebugUtilsMessengerEXT == nullptr || dispatcher->vkDestroyDebugUtilsMessengerEXT == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Vulkan loader does not expose VK_EXT_debug_utils")};
    }
    return detail::checkedCreation(instance.createDebugUtilsMessengerEXT(validationMessengerCreateInfo()));
}

auto makeWindowSurface(const WindowSurface* surface, const vk::raii::Instance& instance)
    -> Result<std::optional<vk::raii::SurfaceKHR>>
{
    if (surface == nullptr)
    {
        return std::nullopt;
    }
    auto created = surface->createSurface(*instance);
    if (!created)
    {
        return std::unexpected {std::move(created.error())};
    }
    if (*created == VkSurfaceKHR {})
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "WindowSurface returned a null Vulkan surface")};
    }
    return vk::raii::SurfaceKHR {instance, *created};
}

auto createLogicalDevice(const vk::raii::PhysicalDevice& physicalDevice, const vk::DeviceCreateInfo& info)
    -> Result<vk::raii::Device>
{
    auto device = vk::Device {};
    const auto result = (*physicalDevice).createDevice(&info, nullptr, &device, *physicalDevice.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(result)};
    }
    // Unlike leaf objects, Device construction uses the handle immediately
    // to load Vulkan functions, so check the result before creating the RAII wrapper.
    return vk::raii::Device {physicalDevice, static_cast<VkDevice>(device)};
}

auto createDevice(const vk::raii::Instance& instance,
                  const detail::SelectedDevice& selected,
                  bool windowed,
                  const vk::PhysicalDeviceFeatures2& features) -> Result<vk::raii::Device>
{
    static constexpr auto priority = 1.0F;
    const auto queueInfos = std::array {
        vk::DeviceQueueCreateInfo {.queueFamilyIndex = selected.info.queueFamily,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority},
        vk::DeviceQueueCreateInfo {.queueFamilyIndex = selected.info.presentQueueFamily,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority}
    };
    const auto queueCount = selected.info.presentQueueFamily == selected.info.queueFamily ? 1U : 2U;
    const auto deviceExtensions =
        std::array {vk::KHRSwapchainExtensionName,
                    selected.info.swapchainMaintenance == ContextDeviceInfo::SwapchainMaintenance::Khr
                        ? vk::KHRSwapchainMaintenance1ExtensionName
                        : vk::EXTSwapchainMaintenance1ExtensionName};
    const auto deviceInfo =
        vk::DeviceCreateInfo {.pNext = &features,
                              .queueCreateInfoCount = queueCount,
                              .pQueueCreateInfos = queueInfos.data(),
                              .enabledExtensionCount = windowed ? 2U : 0U,
                              .ppEnabledExtensionNames = windowed ? deviceExtensions.data() : nullptr};
    const auto physicalDevice =
        vk::raii::PhysicalDevice {instance, static_cast<VkPhysicalDevice>(selected.physicalDevice)};
    return createLogicalDevice(physicalDevice, deviceInfo);
}

auto makeDevice(const vk::raii::Instance& instance, const detail::SelectedDevice& selected, bool windowed)
    -> Result<vk::raii::Device>
{
    auto maintenance = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR {.swapchainMaintenance1 = vk::True};
    auto vulkan13 = vk::PhysicalDeviceVulkan13Features {.pNext = windowed ? &maintenance : nullptr,
                                                        .synchronization2 = vk::True,
                                                        .dynamicRendering = vk::True};
    auto vulkan12 = vk::PhysicalDeviceVulkan12Features {.pNext = &vulkan13, .timelineSemaphore = vk::True};
    const auto features = vk::PhysicalDeviceFeatures2 {.pNext = &vulkan12};
    return createDevice(instance, selected, windowed, features);
}

}

auto Context::InstanceServices::create(const vk::raii::Context& loader,
                                       const WindowSurface* surface,
                                       const ContextOptions& options) -> Result<InstanceServices>
{
    auto extensions = detail::instanceExtensions(surface, options.enableValidation);
    if (!extensions)
    {
        return std::unexpected {std::move(extensions.error())};
    }
    auto instance = makeInstance(loader, options, *extensions);
    if (!instance)
    {
        return std::unexpected {std::move(instance.error())};
    }
    auto debugMessenger = makeDebugMessenger(*instance, options.enableValidation);
    if (!debugMessenger)
    {
        return std::unexpected {std::move(debugMessenger.error())};
    }
    auto ownedSurface = makeWindowSurface(surface, *instance);
    if (!ownedSurface)
    {
        return std::unexpected {std::move(ownedSurface.error())};
    }
    return InstanceServices {.extensions = std::move(*extensions),
                             .instance = std::move(*instance),
                             .debugMessenger = std::move(*debugMessenger),
                             .surface = std::move(*ownedSurface)};
}

namespace
{
auto makePresentation(WindowSurface* surface,
                      const detail::SelectedDevice& selected,
                      const vk::raii::Device& device,
                      vk::SurfaceKHR nativeSurface) -> Result<std::unique_ptr<detail::Presentation>>
{
    if (surface == nullptr)
    {
        return std::unique_ptr<detail::Presentation> {};
    }
    return detail::Presentation::create(selected.physicalDevice, device, nativeSurface, *surface, selected.info);
}
}

class Context::Impl
{
public:
    [[nodiscard]] auto initializeDevice(WindowSurface* surface) -> Result<void>;

private:
    friend class Context;

    vk::raii::Context _loader {vkGetInstanceProcAddr};
    InstanceServices _services;
    vk::raii::Device _device {nullptr};
    std::unique_ptr<detail::Presentation> _presentation;
    ContextDeviceInfo _info;
};

auto Context::Impl::initializeDevice(WindowSurface* surface) -> Result<void>
{
    const auto nativeSurface = _services.surface ? **_services.surface : vk::SurfaceKHR {};
    auto selected = detail::selectDevice(*_services.instance, nativeSurface, _services.extensions);
    if (!selected)
    {
        return std::unexpected {std::move(selected.error())};
    }
    auto createdDevice = makeDevice(_services.instance, *selected, surface != nullptr);
    if (!createdDevice)
    {
        return std::unexpected {std::move(createdDevice.error())};
    }
    _device = std::move(*createdDevice);
    auto createdPresentation = makePresentation(surface, *selected, _device, nativeSurface);
    if (!createdPresentation)
    {
        return std::unexpected {std::move(createdPresentation.error())};
    }
    _presentation = std::move(*createdPresentation);
    _info = std::move(selected->info);
    return {};
}

Context::Context(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)}
{
}

Context::Context(Context&& other) noexcept
    : _implementation {std::move(other._implementation)}
{
}

Context::~Context() noexcept
{
    if (_implementation)
    {
        if (_implementation->_presentation)
        {
            expect(_implementation->_presentation->drain(), "Cannot safely drain window presentation");
        }
    }
}

auto Context::create(const ContextOptions& options) -> Result<Context>
{
    return createInternal(nullptr, options);
}

auto Context::createWithSurface(WindowSurface& surface, const ContextOptions& options) -> Result<Context>
{
    return createInternal(&surface, options);
}

auto Context::createInternal(WindowSurface* surface, const ContextOptions& options) -> Result<Context>
{
    auto implementation = std::make_unique<Impl>();
    if (const auto checked = verifyPrerequisites(implementation->_loader, options); !checked)
    {
        return std::unexpected {checked.error()};
    }
    auto services = InstanceServices::create(implementation->_loader, surface, options);
    if (!services)
    {
        return std::unexpected {std::move(services.error())};
    }
    implementation->_services = std::move(*services);
    if (const auto initialized = implementation->initializeDevice(surface); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return Context {std::move(implementation)};
}

auto Context::isValid() const noexcept -> bool
{
    return _implementation != nullptr;
}

auto Context::getDeviceInfo() const -> ContextDeviceInfo
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _implementation->_info;
}

auto Context::presentClearFrame() const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    if (!_implementation->_presentation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "A window surface is required to present a frame")};
    }
    return _implementation->_presentation->presentClearFrame();
}

}

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
#include <iterator>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DeviceSelection.hpp"
#include "Presentation.hpp"
#include "VulkanError.hpp"

namespace panda
{
// This owner type is stored in Context::Impl; external linkage avoids GCC's
// -Wsubobject-linkage error when the source is compiled as part of a unity build.
// NOLINTNEXTLINE(misc-use-internal-linkage)
class ContextDebugMessengerOwner final
{
public:
    [[nodiscard]] static auto create(VkInstance instance,
                                     PFN_vkCreateDebugUtilsMessengerEXT createFunction,
                                     PFN_vkDestroyDebugUtilsMessengerEXT destroyFunction,
                                     const VkDebugUtilsMessengerCreateInfoEXT& createInfo)
        -> Result<std::unique_ptr<ContextDebugMessengerOwner>>
    {
        auto owner = std::make_unique<ContextDebugMessengerOwner>(instance, destroyFunction);
        auto* messenger = VkDebugUtilsMessengerEXT {};
        const auto result = createFunction(instance, &createInfo, nullptr, &messenger);
        if (result != static_cast<VkResult>(vk::Result::eSuccess))
        {
            return std::unexpected {detail::makeVulkanError(static_cast<vk::Result>(result))};
        }
        owner->_messenger = messenger;
        return owner;
    }

    ContextDebugMessengerOwner(VkInstance instance, PFN_vkDestroyDebugUtilsMessengerEXT destroy) noexcept
        : _instance {instance},
          _destroy {destroy}
    {
    }

    ContextDebugMessengerOwner(const ContextDebugMessengerOwner&) = delete;
    auto operator=(const ContextDebugMessengerOwner&) -> ContextDebugMessengerOwner& = delete;
    ContextDebugMessengerOwner(ContextDebugMessengerOwner&&) = delete;
    auto operator=(ContextDebugMessengerOwner&&) -> ContextDebugMessengerOwner& = delete;

    ~ContextDebugMessengerOwner() noexcept
    {
        if (_messenger != VkDebugUtilsMessengerEXT {} && _destroy != nullptr)
        {
            _destroy(_instance, _messenger, nullptr);
        }
    }

private:
    VkInstance _instance {};
    PFN_vkDestroyDebugUtilsMessengerEXT _destroy {};
    VkDebugUtilsMessengerEXT _messenger {};
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

struct DebugUtilsFunctions
{
    PFN_vkCreateDebugUtilsMessengerEXT create {};
    PFN_vkDestroyDebugUtilsMessengerEXT destroy {};
};

auto loadDebugUtilsFunctions(vk::Instance instance) -> Result<DebugUtilsFunctions>
{
    // Vulkan exposes extension commands through vkGetInstanceProcAddr, not linked exports.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(instance.getProcAddr(
        "vkDestroyDebugUtilsMessengerEXT"));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (create == nullptr || destroy == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Vulkan loader does not expose VK_EXT_debug_utils")};
    }
    return DebugUtilsFunctions {.create = create, .destroy = destroy};
}

auto hasValidationLayer() -> Result<bool>
{
    const auto listed = vk::enumerateInstanceLayerProperties();
    if (listed.result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(listed.result)};
    }
    return std::ranges::any_of(listed.value, [](const auto& layer) {
        return std::string_view {layer.layerName.data()} == "VK_LAYER_KHRONOS_validation";
    });
}

auto verifyPrerequisites(const ContextOptions& options) -> Result<void>
{
    const auto version = vk::enumerateInstanceVersion();
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
        const auto available = hasValidationLayer();
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

auto makeInstance(const ContextOptions& options, const detail::InstanceExtensions& extensions)
    -> Result<vk::UniqueInstance>
{
    auto extensionNames = std::vector<const char*> {};
    extensionNames.reserve(extensions.names.size());
    std::ranges::transform(extensions.names, std::back_inserter(extensionNames), [](const auto& name) {
        return name.c_str();
    });
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
    auto created = detail::createOwned<vk::Instance>(
        [&](vk::Instance* output) {
            return vk::createInstance(&instanceInfo, nullptr, output);
        },
        detail::parentlessDeleter());
    if (created.result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(created.result)};
    }
    return std::move(created.value);
}

auto makeDebugMessenger(vk::Instance instance, bool enableValidation)
    -> Result<std::unique_ptr<ContextDebugMessengerOwner>>
{
    if (!enableValidation)
    {
        return std::unique_ptr<ContextDebugMessengerOwner> {};
    }
    auto functions = loadDebugUtilsFunctions(instance);
    if (!functions)
    {
        return std::unexpected {std::move(functions.error())};
    }
    const auto createInfo = validationMessengerCreateInfo();
    auto* const nativeInstance = static_cast<VkInstance>(instance);
    return ContextDebugMessengerOwner::create(nativeInstance, functions->create, functions->destroy, createInfo);
}

auto makeWindowSurface(const WindowSurface* surface, vk::Instance instance) -> Result<vk::UniqueSurfaceKHR>
{
    if (surface == nullptr)
    {
        return vk::UniqueSurfaceKHR {};
    }
    auto created = surface->createSurface(instance);
    if (!created)
    {
        return std::unexpected {std::move(created.error())};
    }
    if (*created == VkSurfaceKHR {})
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "WindowSurface returned a null Vulkan surface")};
    }
    return vk::UniqueSurfaceKHR {vk::SurfaceKHR {*created}, detail::instanceDeleter(instance)};
}

auto makeDevice(const detail::SelectedDevice& selected, bool windowed) -> Result<vk::UniqueDevice>
{
    auto maintenance = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR {.swapchainMaintenance1 = vk::True};
    auto vulkan13 = vk::PhysicalDeviceVulkan13Features {.pNext = windowed ? &maintenance : nullptr,
                                                        .synchronization2 = vk::True,
                                                        .dynamicRendering = vk::True};
    auto vulkan12 = vk::PhysicalDeviceVulkan12Features {.pNext = &vulkan13, .timelineSemaphore = vk::True};
    const auto features = vk::PhysicalDeviceFeatures2 {.pNext = &vulkan12};
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
    auto created = detail::createOwned<vk::Device>(
        [&](vk::Device* output) {
            return selected.physicalDevice.createDevice(&deviceInfo, nullptr, output);
        },
        detail::parentlessDeleter());
    if (created.result != vk::Result::eSuccess)
    {
        return std::unexpected {detail::makeVulkanError(created.result)};
    }
    return std::move(created.value);
}
}

struct Context::Impl
{
    vk::UniqueInstance instance;
    std::unique_ptr<ContextDebugMessengerOwner> debugMessenger;
    vk::UniqueSurfaceKHR surface;
    vk::UniqueDevice device;
    std::unique_ptr<detail::Presentation> presentation;
    ContextDeviceInfo info;
};

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
        if (_implementation->presentation)
        {
            expect(_implementation->presentation->drain(), "Cannot safely drain window presentation");
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
    if (const auto checked = verifyPrerequisites(options); !checked)
    {
        return std::unexpected {checked.error()};
    }
    auto extensions = detail::instanceExtensions(surface, options.enableValidation);
    if (!extensions)
    {
        return std::unexpected {std::move(extensions.error())};
    }
    auto instance = makeInstance(options, *extensions);
    if (!instance)
    {
        return std::unexpected {std::move(instance.error())};
    }
    auto debugMessenger = makeDebugMessenger(**instance, options.enableValidation);
    if (!debugMessenger)
    {
        return std::unexpected {std::move(debugMessenger.error())};
    }
    auto ownedSurface = makeWindowSurface(surface, **instance);
    if (!ownedSurface)
    {
        return std::unexpected {std::move(ownedSurface.error())};
    }
    auto* const nativeSurface = static_cast<VkSurfaceKHR>(ownedSurface->get());
    auto selected = detail::selectDevice(**instance, nativeSurface, *extensions);
    if (!selected)
    {
        return std::unexpected {std::move(selected.error())};
    }
    auto device = makeDevice(*selected, surface != nullptr);
    if (!device)
    {
        return std::unexpected {std::move(device.error())};
    }
    auto implementation = std::make_unique<Impl>();
    implementation->instance = std::move(*instance);
    implementation->debugMessenger = std::move(*debugMessenger);
    implementation->surface = std::move(*ownedSurface);
    implementation->device = std::move(*device);
    implementation->info = std::move(selected->info);
    if (surface != nullptr)
    {
        auto presentation = detail::Presentation::create(selected->physicalDevice,
                                                         *implementation->device,
                                                         nativeSurface,
                                                         *surface,
                                                         implementation->info);
        if (!presentation)
        {
            return std::unexpected {std::move(presentation.error())};
        }
        implementation->presentation = std::move(*presentation);
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
    return _implementation->info;
}

auto Context::presentClearFrame() const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    if (!_implementation->presentation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "A window surface is required to present a frame")};
    }
    return _implementation->presentation->presentClearFrame();
}

}

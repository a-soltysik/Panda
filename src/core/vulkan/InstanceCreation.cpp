// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "InstanceCreation.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ContextResources.hpp"
#include "DeviceSelection.hpp"
#include "VulkanError.hpp"

namespace panda::core::vulkan::instance
{
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

auto verifyValidationLayer(const vk::raii::Context& loader, bool enabled) -> Result<void>
{
    if (!enabled)
    {
        return {};
    }
    const auto available = hasValidationLayer(loader);
    if (!available)
    {
        return std::unexpected {available.error()};
    }
    if (!*available)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "VK_LAYER_KHRONOS_validation is unavailable")};
    }
    return {};
}

auto verifyPrerequisites(const vk::raii::Context& loader, const ContextOptions& options) -> Result<void>
{
    const auto version = loader.enumerateInstanceVersion();
    if (version.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(version.result)};
    }
    if (version.value < vk::ApiVersion13)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Vulkan loader does not support version 1.3")};
    }
    return verifyValidationLayer(loader, options.enableValidation);
}

auto createInstance(const vk::raii::Context& loader, const vk::InstanceCreateInfo& info) -> Result<vk::raii::Instance>
{
    auto instance = vk::Instance {};
    const auto result = vk::createInstance(&info, nullptr, &instance, *loader.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    // Unlike leaf objects, Instance construction uses the handle immediately
    // to load Vulkan functions, so check the result before creating the RAII wrapper.
    return vk::raii::Instance {loader, instance};
}

auto makeInstance(const vk::raii::Context& loader, const ContextOptions& options, const InstanceExtensions& extensions)
    -> Result<vk::raii::Instance>
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
    if (!*created)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "WindowSurface returned a null Vulkan surface")};
    }
    return vk::raii::SurfaceKHR {instance, *created};
}

auto makeInstanceResources(const vk::raii::Context& loader, const WindowSurface* surface, const ContextOptions& options)
    -> Result<InstanceResources>
{
    auto extensions = instanceExtensions(surface, options.enableValidation);
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
    return InstanceResources {.extensions = std::move(*extensions),
                              .instance = std::move(*instance),
                              .debugMessenger = std::move(*debugMessenger),
                              .surface = std::move(*ownedSurface)};
}
}

auto createResources(const vk::raii::Context& loader, const WindowSurface* surface, const ContextOptions& options)
    -> Result<InstanceResources>
{
    if (const auto checked = verifyPrerequisites(loader, options); !checked)
    {
        return std::unexpected {checked.error()};
    }
    return makeInstanceResources(loader, surface, options);
}
}

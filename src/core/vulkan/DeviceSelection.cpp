// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>
// clang-format on

#include "DeviceSelection.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Logger.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "VulkanError.hpp"

namespace panda::detail
{
namespace
{
auto hasExtension(const std::vector<vk::ExtensionProperties>& available, std::string_view name) -> bool
{
    return std::ranges::any_of(available, [name](const auto& extension) {
        return std::string_view {extension.extensionName.data()} == name;
    });
}

void appendUnique(std::vector<std::string>& names, std::string_view name)
{
    if (std::ranges::find(names, name) == names.end())
    {
        names.emplace_back(name);
    }
}

auto checkWindowExtensions(const std::vector<std::string>& required,
                           const std::vector<vk::ExtensionProperties>& available) -> Result<void>
{
    const auto missing = std::ranges::find_if(required, [&available](const auto& name) {
        return !hasExtension(available, name);
    });
    if (missing != required.end())
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Required window instance extension is unavailable: " + *missing)};
    }
    return {};
}

auto availableInstanceExtensions() -> Result<std::vector<vk::ExtensionProperties>>
{
    const auto available = vk::enumerateInstanceExtensionProperties();
    if (available.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(available.result)};
    }
    return available.value;
}

auto appendMaintenanceExtensions(InstanceExtensions& chosen, const std::vector<vk::ExtensionProperties>& available)
    -> Result<void>
{
    if (!hasExtension(available, vk::KHRGetSurfaceCapabilities2ExtensionName))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported,
                                          "VK_KHR_get_surface_capabilities2 is required for swapchain maintenance1")};
    }
    chosen.hasKhrMaintenance = hasExtension(available, vk::KHRSurfaceMaintenance1ExtensionName);
    chosen.hasExtMaintenance = hasExtension(available, vk::EXTSurfaceMaintenance1ExtensionName);
    if (!chosen.hasKhrMaintenance && !chosen.hasExtMaintenance)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "No KHR or EXT surface maintenance1 instance extension is available")};
    }
    appendUnique(chosen.names, vk::KHRGetSurfaceCapabilities2ExtensionName);
    if (chosen.hasKhrMaintenance)
    {
        appendUnique(chosen.names, vk::KHRSurfaceMaintenance1ExtensionName);
    }
    if (chosen.hasExtMaintenance)
    {
        appendUnique(chosen.names, vk::EXTSurfaceMaintenance1ExtensionName);
    }
    return {};
}

auto requiredWindowExtensions(const WindowSurface* surface) -> Result<std::vector<std::string>>
{
    if (surface == nullptr)
    {
        return std::vector<std::string> {};
    }
    auto required = surface->getRequiredInstanceExtensions();
    if (!required)
    {
        return std::unexpected {std::move(required.error())};
    }
    if (std::ranges::find(*required, vk::KHRSurfaceExtensionName) == required->end())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "WindowSurface must require VK_KHR_surface")};
    }
    return std::move(*required);
}

auto appendValidationExtension(InstanceExtensions& chosen,
                               bool enableValidation,
                               const std::vector<vk::ExtensionProperties>& available) -> Result<void>
{
    if (!enableValidation)
    {
        return {};
    }
    if (!hasExtension(available, vk::EXTDebugUtilsExtensionName))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "VK_EXT_debug_utils is unavailable for validation diagnostics")};
    }
    appendUnique(chosen.names, vk::EXTDebugUtilsExtensionName);
    return {};
}

}

auto instanceExtensions(const WindowSurface* surface, bool enableValidation) -> Result<InstanceExtensions>
{
    if (surface == nullptr && !enableValidation)
    {
        return InstanceExtensions {};
    }
    auto required = requiredWindowExtensions(surface);
    if (!required)
    {
        return std::unexpected {std::move(required.error())};
    }
    auto available = availableInstanceExtensions();
    if (!available)
    {
        return std::unexpected {std::move(available.error())};
    }
    if (surface != nullptr)
    {
        if (const auto checked = checkWindowExtensions(*required, *available); !checked)
        {
            return std::unexpected {checked.error()};
        }
    }
    auto chosen = InstanceExtensions {.names = std::move(*required)};
    if (surface != nullptr)
    {
        if (const auto appended = appendMaintenanceExtensions(chosen, *available); !appended)
        {
            return std::unexpected {appended.error()};
        }
    }
    if (const auto appended = appendValidationExtension(chosen, enableValidation, *available); !appended)
    {
        return std::unexpected {appended.error()};
    }
    return chosen;
}

namespace
{
using Maintenance = ContextDeviceInfo::SwapchainMaintenance;

auto maintenanceVariant(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<std::optional<Maintenance>, Error>
{
    if (surface == VkSurfaceKHR {})
    {
        return std::optional {Maintenance::None};
    }
    const auto listed = physicalDevice.enumerateDeviceExtensionProperties();
    if (listed.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(listed.result)};
    }
    if (!hasExtension(listed.value, vk::KHRSwapchainExtensionName))
    {
        return std::nullopt;
    }
    if (extensions.hasKhrMaintenance && hasExtension(listed.value, vk::KHRSwapchainMaintenance1ExtensionName))
    {
        return std::optional {Maintenance::Khr};
    }
    if (extensions.hasExtMaintenance && hasExtension(listed.value, vk::EXTSwapchainMaintenance1ExtensionName))
    {
        return std::optional {Maintenance::Ext};
    }
    return std::nullopt;
}

auto supportsRequiredFeatures(vk::PhysicalDevice physicalDevice, bool windowed) -> bool
{
    auto maintenance = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR {};
    auto vulkan13 = vk::PhysicalDeviceVulkan13Features {.pNext = windowed ? &maintenance : nullptr};
    auto vulkan12 = vk::PhysicalDeviceVulkan12Features {.pNext = &vulkan13};
    auto features = vk::PhysicalDeviceFeatures2 {.pNext = &vulkan12};
    physicalDevice.getFeatures2(&features);
    return vulkan12.timelineSemaphore == vk::True && vulkan13.dynamicRendering == vk::True &&
           vulkan13.synchronization2 == vk::True && (!windowed || maintenance.swapchainMaintenance1 == vk::True);
}

auto graphicsCompute(vk::QueueFamilyProperties family) -> bool
{
    static constexpr auto required = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
    return family.queueCount != 0 && (family.queueFlags & required) == required;
}

auto presentSupport(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface, std::uint32_t family)
    -> std::expected<bool, Error>
{
    const auto supported = physicalDevice.getSurfaceSupportKHR(family, vk::SurfaceKHR {surface});
    if (supported.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(supported.result)};
    }
    return supported.value == vk::True;
}

struct QueueSelection
{
    std::uint32_t graphics;
    std::uint32_t present;
};

auto selectHeadlessQueue(const std::vector<vk::QueueFamilyProperties>& families) -> std::optional<QueueSelection>
{
    const auto selected = std::ranges::find_if(families, graphicsCompute);
    if (selected == families.end())
    {
        return std::nullopt;
    }
    const auto family = static_cast<std::uint32_t>(std::distance(families.begin(), selected));
    return QueueSelection {.graphics = family, .present = family};
}

struct QueueCandidates
{
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
};

auto inspectWindowQueue(vk::PhysicalDevice physicalDevice,
                        VkSurfaceKHR surface,
                        vk::QueueFamilyProperties family,
                        std::uint32_t index,
                        QueueCandidates& candidates) -> std::expected<bool, Error>
{
    if (family.queueCount == 0)
    {
        return false;
    }
    const auto graphics = graphicsCompute(family);
    if (graphics && !candidates.graphics)
    {
        candidates.graphics = index;
    }
    const auto supported = presentSupport(physicalDevice, surface, index);
    if (!supported)
    {
        return std::unexpected {supported.error()};
    }
    if (*supported && !candidates.present)
    {
        candidates.present = index;
    }
    return graphics && *supported;
}

auto selectWindowQueues(vk::PhysicalDevice physicalDevice,
                        VkSurfaceKHR surface,
                        const std::vector<vk::QueueFamilyProperties>& families)
    -> std::expected<std::optional<QueueSelection>, Error>
{
    auto candidates = QueueCandidates {};
    for (auto index = std::size_t {0}; index < families.size(); ++index)
    {
        const auto family = static_cast<std::uint32_t>(index);
        const auto combined = inspectWindowQueue(physicalDevice, surface, families.at(index), family, candidates);
        if (!combined)
        {
            return std::unexpected {combined.error()};
        }
        if (*combined)
        {
            return std::optional {
                QueueSelection {.graphics = family, .present = family}
            };
        }
    }
    if (!candidates.graphics || !candidates.present)
    {
        return std::nullopt;
    }
    return std::optional {
        QueueSelection {.graphics = *candidates.graphics, .present = *candidates.present}
    };
}

auto selectQueues(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface)
    -> std::expected<std::optional<QueueSelection>, Error>
{
    const auto families = physicalDevice.getQueueFamilyProperties();
    if (surface == VkSurfaceKHR {})
    {
        return selectHeadlessQueue(families);
    }
    return selectWindowQueues(physicalDevice, surface, families);
}

auto supportsSurfaceFormats(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface) -> std::expected<bool, Error>
{
    if (surface == VkSurfaceKHR {})
    {
        return true;
    }
    const auto formats = physicalDevice.getSurfaceFormatsKHR(vk::SurfaceKHR {surface});
    if (formats.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(formats.result)};
    }
    const auto modes = physicalDevice.getSurfacePresentModesKHR(vk::SurfaceKHR {surface});
    if (modes.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(modes.result)};
    }
    return !formats.value.empty() && !modes.value.empty();
}

struct DeviceProfile
{
    vk::PhysicalDeviceProperties properties;
    Maintenance maintenance;
};

auto inspectDeviceProfile(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<std::optional<DeviceProfile>, Error>
{
    auto properties = physicalDevice.getProperties();
    if (properties.apiVersion < vk::ApiVersion13)
    {
        return std::nullopt;
    }
    const auto variant = maintenanceVariant(physicalDevice, surface, extensions);
    if (!variant)
    {
        return std::unexpected {variant.error()};
    }
    if (!*variant || !supportsRequiredFeatures(physicalDevice, surface != VkSurfaceKHR {}))
    {
        return std::nullopt;
    }
    return std::optional {
        DeviceProfile {.properties = std::move(properties), .maintenance = **variant}
    };
}

auto inspectPresentation(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface)
    -> std::expected<std::optional<QueueSelection>, Error>
{
    const auto queues = selectQueues(physicalDevice, surface);
    if (!queues)
    {
        return std::unexpected {queues.error()};
    }
    if (!*queues)
    {
        return std::nullopt;
    }
    if (const auto formats = supportsSurfaceFormats(physicalDevice, surface); !formats)
    {
        return std::unexpected {formats.error()};
    }
    else if (!*formats)
    {
        return std::nullopt;
    }
    return **queues;
}

auto inspectDevice(vk::PhysicalDevice physicalDevice, VkSurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<std::optional<SelectedDevice>, Error>
{
    auto profile = inspectDeviceProfile(physicalDevice, surface, extensions);
    if (!profile)
    {
        return std::unexpected {profile.error()};
    }
    if (!*profile)
    {
        return std::nullopt;
    }
    auto presentation = inspectPresentation(physicalDevice, surface);
    if (!presentation)
    {
        return std::unexpected {presentation.error()};
    }
    if (!*presentation)
    {
        return std::nullopt;
    }
    const auto& [properties, maintenance] = **profile;
    const auto& [graphics, present] = **presentation;
    return std::optional {
        SelectedDevice {.physicalDevice = physicalDevice,
                        .info = {.name = properties.deviceName.data(),
                                 .apiVersion = properties.apiVersion,
                                 .queueFamily = graphics,
                                 .presentQueueFamily = present,
                                 .swapchainMaintenance = maintenance}}
    };
}

auto isDeviceInspectionFailureFatal(ErrorCode code) -> bool
{
    switch (code)
    {
    case ErrorCode::SurfaceLost:
    case ErrorCode::InvalidArgument:
        return true;
    case ErrorCode::Unsupported:
    case ErrorCode::ResourceExhausted:
    case ErrorCode::Timeout:
    case ErrorCode::DeviceLost:
    case ErrorCode::BackendFailure:
        return false;
    }
    return true;
}

auto handleInspectionFailure(vk::PhysicalDevice physicalDevice, Error error, std::optional<Error>& firstError)
    -> std::expected<void, Error>
{
    if (isDeviceInspectionFailureFatal(error.code))
    {
        return std::unexpected {std::move(error)};
    }
    const auto properties = physicalDevice.getProperties();
    log::warning("Skipping Vulkan physical device '{}' after inspection failed: {}",
                 properties.deviceName.data(),
                 error.message);
    if (!firstError)
    {
        firstError = std::move(error);
    }
    return {};
}

auto inspectDevices(const std::vector<vk::PhysicalDevice>& devices,
                    VkSurfaceKHR surface,
                    const InstanceExtensions& extensions) -> std::expected<std::vector<SelectedDevice>, Error>
{
    auto candidates = std::vector<SelectedDevice> {};
    auto inspectionError = std::optional<Error> {};
    for (const auto physicalDevice : devices)
    {
        auto inspected = inspectDevice(physicalDevice, surface, extensions);
        if (!inspected)
        {
            auto handled = handleInspectionFailure(physicalDevice, std::move(inspected.error()), inspectionError);
            if (!handled)
            {
                return std::unexpected {std::move(handled.error())};
            }
            continue;
        }
        if (*inspected)
        {
            candidates.push_back(std::move(**inspected));
        }
    }
    if (candidates.empty() && inspectionError)
    {
        return std::unexpected {std::move(*inspectionError)};
    }
    return candidates;
}

}

auto selectDevice(vk::Instance instance, VkSurfaceKHR surface, const InstanceExtensions& extensions)
    -> std::expected<SelectedDevice, Error>
{
    const auto listed = instance.enumeratePhysicalDevices();
    if (listed.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(listed.result)};
    }
    auto candidates = inspectDevices(listed.value, surface, extensions);
    if (!candidates)
    {
        return std::unexpected {std::move(candidates.error())};
    }
    if (candidates->empty())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported,
                                          surface == VkSurfaceKHR {}
                                              ? "No Vulkan 1.3 device supports graphics/compute, dynamic rendering, "
                                                "synchronization2 and timeline semaphores"
                                              : "No Vulkan 1.3 device supports the window surface, swapchain "
                                                "maintenance1 and required core features")};
    }
    const auto discrete = std::ranges::find_if(*candidates, [](const auto& candidate) {
        return candidate.physicalDevice.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
    });
    return discrete != candidates->end() ? std::move(*discrete) : std::move(candidates->front());
}

}

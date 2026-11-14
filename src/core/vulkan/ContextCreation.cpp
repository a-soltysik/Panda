// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "ContextCreation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <utility>
#include <vector>

#include "ContextResources.hpp"
#include "DeviceSelection.hpp"
#include "InstanceCreation.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalContext.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/Presentation.hpp"
#include "renderer/FrameBindings.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::context
{
namespace api_detail = ::panda::detail;

namespace
{
struct DeviceResourceOptions
{
    std::optional<api_detail::ExternalSupport> externalSupport;
    bool enableFramePerformanceSampling {};
};

template <typename T>
auto borrowIfPresent(const std::unique_ptr<T>& owner) noexcept -> std::optional<std::reference_wrapper<T>>
{
    if (!owner)
    {
        return std::nullopt;
    }
    return std::ref(*owner);
}

auto makeDeviceExtensions(const SelectedDevice& selected,
                          bool windowed,
                          const std::vector<std::string>& extraExtensions) -> std::vector<const char*>
{
    auto extensions = std::vector<const char*> {};
    if (windowed)
    {
        extensions.push_back(vk::KHRSwapchainExtensionName);
        extensions.push_back(selected.info.swapchainMaintenance == ContextDeviceInfo::SwapchainMaintenance::Khr
                                 ? vk::KHRSwapchainMaintenance1ExtensionName
                                 : vk::EXTSwapchainMaintenance1ExtensionName);
    }
    std::ranges::transform(extraExtensions, std::back_inserter(extensions), [](const auto& extension) {
        return extension.c_str();
    });
    return extensions;
}

auto createLogicalDevice(const vk::raii::PhysicalDevice& physicalDevice, const vk::DeviceCreateInfo& info)
    -> Result<vk::raii::Device>
{
    auto device = vk::Device {};
    const auto result = (*physicalDevice).createDevice(&info, nullptr, &device, *physicalDevice.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    // Unlike leaf objects, Device construction uses the handle immediately to load Vulkan functions.
    return vk::raii::Device {physicalDevice, device};
}

auto makeDevice(const vk::raii::PhysicalDevice& physicalDevice,
                const SelectedDevice& selected,
                bool windowed,
                const std::vector<std::string>& extraExtensions) -> Result<vk::raii::Device>
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
    auto maintenance = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR {.swapchainMaintenance1 = vk::True};
    auto vulkan13 = vk::PhysicalDeviceVulkan13Features {.pNext = windowed ? &maintenance : nullptr,
                                                        .synchronization2 = vk::True,
                                                        .dynamicRendering = vk::True};
    auto vulkan12 = vk::PhysicalDeviceVulkan12Features {.pNext = &vulkan13, .timelineSemaphore = vk::True};
    const auto features = vk::PhysicalDeviceFeatures2 {.pNext = &vulkan12};
    const auto deviceExtensions = makeDeviceExtensions(selected, windowed, extraExtensions);
    const auto deviceInfo =
        vk::DeviceCreateInfo {.pNext = &features,
                              .queueCreateInfoCount = queueCount,
                              .pQueueCreateInfos = queueInfos.data(),
                              .enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size()),
                              .ppEnabledExtensionNames = deviceExtensions.data()};
    return createLogicalDevice(physicalDevice, deviceInfo);
}

auto initializeDevice(ContextResources& resources, const WindowSurface* surface, const ContextOptions& options)
    -> Result<void>;
auto selectPhysicalDevice(ContextResources& resources, std::uint32_t maxLights) -> Result<SelectedDevice>;
auto queryExternalSupport(ContextResources& resources, const ContextOptions& options)
    -> Result<std::optional<api_detail::ExternalSupport>>;
auto initializeLogicalDevice(ContextResources& resources,
                             const SelectedDevice& selected,
                             bool windowed,
                             const std::optional<api_detail::ExternalSupport>& externalSupport) -> Result<void>;
auto initializeSelectedDevice(ContextResources& resources,
                              const WindowSurface* surface,
                              const SelectedDevice& selected,
                              const ContextOptions& options) -> Result<void>;
auto initializeSelectedDeviceResources(ContextResources& resources,
                                       const WindowSurface* surface,
                                       const SelectedDevice& selected,
                                       DeviceResourceOptions options) -> Result<void>;
auto initializePerformance(ContextResources& resources,
                           const SelectedDevice& selected,
                           bool enableFramePerformanceSampling) -> Result<void>;
auto initializePresentation(ContextResources& resources,
                            const WindowSurface& surface,
                            const SelectedDevice& selected,
                            std::uint32_t maxLights) -> Result<void>;
void updateDeviceInfo(ContextResources& resources, const SelectedDevice& selected);
}

auto createResources(const WindowSurface* surface, const ContextOptions& options)
    -> Result<std::unique_ptr<ContextResources>>
{
    auto resources = std::make_unique<ContextResources>();
    auto instanceResources = instance::createResources(resources->loader(), surface, options);
    if (!instanceResources)
    {
        return std::unexpected {instanceResources.error()};
    }
    resources->instance() = std::move(*instanceResources);
    if (const auto initialized = initializeDevice(*resources, surface, options); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return resources;
}

namespace
{
auto selectPhysicalDevice(ContextResources& resources, std::uint32_t maxLights) -> Result<SelectedDevice>
{
    const auto& instanceResources = resources.instance();
    const auto surface = instanceResources.surface ? **instanceResources.surface : vk::SurfaceKHR {};
    auto selected = selectDevice(*instanceResources.instance, surface, instanceResources.extensions);
    if (!selected)
    {
        return std::unexpected {std::move(selected.error())};
    }
    resources.physicalDevice() = vk::raii::PhysicalDevice {resources.instance().instance, selected->physicalDevice};
    if (const auto supported = renderer::FrameBindings::validateDeviceLimits(selected->limits, maxLights); !supported)
    {
        return std::unexpected {supported.error()};
    }
    return selected;
}

auto queryExternalSupport(ContextResources& resources, const ContextOptions& options)
    -> Result<std::optional<api_detail::ExternalSupport>>
{
    if (options.externalBufferSupport != ContextOptions::ExternalBufferSupport::Disabled)
    {
        return api_detail::ExternalContext::querySupport(
            resources.physicalDevice(),
            options.externalBufferSupport == ContextOptions::ExternalBufferSupport::Required);
    }
    return std::nullopt;
}

auto initializeLogicalDevice(ContextResources& resources,
                             const SelectedDevice& selected,
                             bool windowed,
                             const std::optional<api_detail::ExternalSupport>& externalSupport) -> Result<void>
{
    const auto extraExtensions = externalSupport ? externalSupport->deviceExtensions : std::vector<std::string> {};
    auto createdDevice = makeDevice(resources.physicalDevice(), selected, windowed, extraExtensions);
    if (!createdDevice)
    {
        return std::unexpected {std::move(createdDevice.error())};
    }
    resources.device() = std::move(*createdDevice);
    return {};
}

auto initializeDevice(ContextResources& resources, const WindowSurface* surface, const ContextOptions& options)
    -> Result<void>
{
    auto selected = selectPhysicalDevice(resources, options.maxLights);
    if (!selected)
    {
        return std::unexpected {selected.error()};
    }
    auto externalSupport = queryExternalSupport(resources, options);
    if (!externalSupport)
    {
        return std::unexpected {externalSupport.error()};
    }
    if (const auto initialized = initializeLogicalDevice(resources, *selected, surface != nullptr, *externalSupport);
        !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    if (const auto initialized = initializeSelectedDeviceResources(
            resources,
            surface,
            *selected,
            DeviceResourceOptions {.externalSupport = std::move(*externalSupport),
                                   .enableFramePerformanceSampling = options.enableFramePerformanceSampling});
        !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return initializeSelectedDevice(resources, surface, *selected, options);
}

auto initializeSelectedDevice(ContextResources& resources,
                              const WindowSurface* surface,
                              const SelectedDevice& selected,
                              const ContextOptions& options) -> Result<void>
{
    if (const auto initialized = initializePerformance(resources, selected, options.enableFramePerformanceSampling);
        !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    if (surface != nullptr)
    {
        if (const auto initialized = initializePresentation(resources, *surface, selected, options.maxLights);
            !initialized)
        {
            return std::unexpected {initialized.error()};
        }
    }
    updateDeviceInfo(resources, selected);
    return {};
}

void updateDeviceInfo(ContextResources& resources, const SelectedDevice& selected)
{
    resources.info() = selected.info;
    resources.info().externalBufferExportEnabled = resources.externalContext() != nullptr;
    resources.info().externalSemaphoreExportEnabled =
        resources.externalContext() != nullptr && resources.externalContext()->supportsSemaphoreExport();
    const auto properties =
        resources.physicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
    const auto& uuid = properties.get<vk::PhysicalDeviceIDProperties>().deviceUUID;
    std::ranges::copy(uuid, resources.info().deviceUuid.begin());
}

auto initializeSelectedDeviceResources(ContextResources& resources,
                                       const WindowSurface* surface,
                                       const SelectedDevice& selected,
                                       DeviceResourceOptions options) -> Result<void>
{
    resources.memoryPool() = resources::MemoryPool::create(resources.physicalDevice(), resources.device());
    const auto needsExternalHandoff =
        options.externalSupport && options.externalSupport->semaphoreHandleType.has_value();
    const auto needsSubmissionQueue =
        surface != nullptr || needsExternalHandoff || options.enableFramePerformanceSampling;
    if (needsSubmissionQueue)
    {
        resources.submissions() =
            std::make_unique<api_detail::SubmissionQueue>(resources.device(), selected.info.queueFamily);
    }
    if (surface == nullptr && resources.submissions() != nullptr)
    {
        if (const auto initialized = resources.submissions()->initialize(*resources.physicalDevice()); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
    }
    if (options.externalSupport)
    {
        resources.externalContext() = std::make_unique<api_detail::ExternalContext>(
            api_detail::ExternalContext::CreateInfo {.physicalDevice = resources.physicalDevice(),
                                                     .device = resources.device(),
                                                     .pool = *resources.memoryPool(),
                                                     .submissions = borrowIfPresent(resources.submissions()),
                                                     .uploads = resources.uploads(),
                                                     .queueFamily = selected.info.queueFamily,
                                                     .support = std::move(*options.externalSupport)});
    }
    return {};
}

auto initializePerformance(ContextResources& resources,
                           const SelectedDevice& selected,
                           bool enableFramePerformanceSampling) -> Result<void>
{
    auto performance = std::make_unique<api_detail::PerformanceManagerImpl>(
        resources.device(),
        resources.submissions().get(),
        api_detail::TimestampProperties {.validBits = selected.timestampValidBits,
                                         .periodNanoseconds = selected.timestampPeriodNanoseconds},
        enableFramePerformanceSampling);
    if (const auto initialized = performance->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    resources.performanceManagerImplementation() = std::move(performance);
    return {};
}

auto initializePresentation(ContextResources& resources,
                            const WindowSurface& surface,
                            const SelectedDevice& selected,
                            std::uint32_t maxLights) -> Result<void>
{
    const auto& instanceResources = resources.instance();
    const auto contextSurface = instanceResources.surface ? **instanceResources.surface : vk::SurfaceKHR {};
    auto presentation = Presentation::create({.physicalDevice = selected.physicalDevice,
                                              .device = resources.device(),
                                              .surface = contextSurface,
                                              .window = surface,
                                              .deviceInfo = selected.info,
                                              .submissions = *resources.submissions(),
                                              .pool = resources.memoryPool().get(),
                                              .performance = resources.performanceManagerImplementation().get(),
                                              .maxLights = maxLights});
    if (!presentation)
    {
        return std::unexpected {presentation.error()};
    }
    resources.presentation() = std::move(*presentation);
    return {};
}
}
}

// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "EngineCreation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <panda/DeviceInfo.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/vulkan/Error.hpp>
#include <panda/vulkan/WindowSurface.hpp>
#include <string>
#include <utility>
#include <vector>

#include "DeviceContext.hpp"
#include "DeviceSelection.hpp"
#include "EngineCreateInfo.hpp"
#include "InstanceCreation.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRenderBackend.hpp"
#include "presentation/WindowPresentation.hpp"
#include "renderer/FrameBindings.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::engine
{
namespace api_detail = ::panda::detail;

namespace
{
struct DeviceResourceOptions
{
    std::optional<api_detail::ExternalSupport> externalSupport;
};

auto makeDeviceExtensions(const SelectedDevice& selected,
                          bool windowed,
                          const std::vector<std::string>& extraExtensions) -> std::vector<const char*>
{
    auto extensions = std::vector<const char*> {};
    if (windowed)
    {
        extensions.push_back(vk::KHRSwapchainExtensionName);
        extensions.push_back(selected.maintenance == SwapchainMaintenance::Khr
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
        vk::DeviceQueueCreateInfo {.queueFamilyIndex = selected.queueFamily,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority},
        vk::DeviceQueueCreateInfo {.queueFamilyIndex = selected.presentQueueFamily,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority}
    };
    const auto queueCount = selected.presentQueueFamily == selected.queueFamily ? 1U : 2U;
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

auto initializeDevice(EngineCreateInfo& resources, const WindowSurface* surface, const EngineOptions& options)
    -> Result<void>;
auto selectPhysicalDevice(EngineCreateInfo& resources, std::uint32_t maxLights) -> Result<SelectedDevice>;
void populateDeviceUuid(EngineCreateInfo& resources, SelectedDevice& selected);
auto queryExternalSupport(EngineCreateInfo& resources, const EngineOptions& options)
    -> Result<std::optional<api_detail::ExternalSupport>>;
auto initializeLogicalDevice(EngineCreateInfo& resources,
                             const SelectedDevice& selected,
                             bool windowed,
                             const std::optional<api_detail::ExternalSupport>& externalSupport) -> Result<void>;
auto initializeSelectedDevice(EngineCreateInfo& resources,
                              const WindowSurface* surface,
                              const SelectedDevice& selected,
                              const EngineOptions& options) -> Result<void>;
auto initializeSelectedDeviceResources(EngineCreateInfo& resources,
                                       const SelectedDevice& selected,
                                       DeviceResourceOptions options) -> Result<void>;
auto initializePerformance(EngineCreateInfo& resources,
                           const SelectedDevice& selected,
                           bool enableFramePerformanceSampling) -> Result<void>;
auto initializePresentation(EngineCreateInfo& resources,
                            const WindowSurface& surface,
                            const SelectedDevice& selected,
                            std::uint32_t maxLights) -> Result<void>;
void populateEngineDiagnostics(EngineCreateInfo& resources,
                               const SelectedDevice& selected,
                               const EngineOptions& options);
}

auto createResources(const WindowSurface* surface, const EngineOptions& options) -> Result<EngineCreateInfo>
{
    auto resources = EngineCreateInfo {};
    resources.device = std::make_unique<DeviceContext>();
    resources.maxLights = options.maxLights;
    auto instanceResources = instance::createResources(resources.device->loader, surface, options);
    if (!instanceResources)
    {
        return std::unexpected {instanceResources.error()};
    }
    resources.device->instance = std::move(*instanceResources);
    if (const auto initialized = initializeDevice(resources, surface, options); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return resources;
}

namespace
{
auto selectPhysicalDevice(EngineCreateInfo& resources, std::uint32_t maxLights) -> Result<SelectedDevice>
{
    const auto& instanceResources = resources.device->instance;
    const auto surface = instanceResources.surface ? **instanceResources.surface : vk::SurfaceKHR {};
    auto selected = selectDevice(*instanceResources.instance, surface, instanceResources.extensions);
    if (!selected)
    {
        return std::unexpected {std::move(selected.error())};
    }
    resources.device->physicalDevice = vk::raii::PhysicalDevice {instanceResources.instance, selected->physicalDevice};
    if (const auto supported = renderer::FrameBindings::validateDeviceLimits(selected->limits, maxLights); !supported)
    {
        return std::unexpected {supported.error()};
    }
    return selected;
}

auto queryExternalSupport(EngineCreateInfo& resources, const EngineOptions& options)
    -> Result<std::optional<api_detail::ExternalSupport>>
{
    if (options.externalBufferSupport != EngineOptions::ExternalBufferSupport::Disabled)
    {
        return api_detail::ExternalBufferInterop::querySupport(
            resources.device->physicalDevice,
            options.externalBufferSupport == EngineOptions::ExternalBufferSupport::Required);
    }
    return std::nullopt;
}

auto initializeLogicalDevice(EngineCreateInfo& resources,
                             const SelectedDevice& selected,
                             bool windowed,
                             const std::optional<api_detail::ExternalSupport>& externalSupport) -> Result<void>
{
    const auto extraExtensions = externalSupport ? externalSupport->deviceExtensions : std::vector<std::string> {};
    auto createdDevice = makeDevice(resources.device->physicalDevice, selected, windowed, extraExtensions);
    if (!createdDevice)
    {
        return std::unexpected {std::move(createdDevice.error())};
    }
    resources.device->device = std::move(*createdDevice);
    return {};
}

auto initializeDevice(EngineCreateInfo& resources, const WindowSurface* surface, const EngineOptions& options)
    -> Result<void>
{
    auto selected = selectPhysicalDevice(resources, options.maxLights);
    if (!selected)
    {
        return std::unexpected {selected.error()};
    }
    populateDeviceUuid(resources, *selected);
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
    if (const auto initialized =
            initializeSelectedDeviceResources(resources,
                                              *selected,
                                              DeviceResourceOptions {.externalSupport = std::move(*externalSupport)});
        !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return initializeSelectedDevice(resources, surface, *selected, options);
}

auto initializeSelectedDevice(EngineCreateInfo& resources,
                              const WindowSurface* surface,
                              const SelectedDevice& selected,
                              const EngineOptions& options) -> Result<void>
{
    if (const auto initialized = initializePerformance(resources, selected, options.enableFramePerformanceSampling);
        !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    resources.offscreenRenderBackend = std::make_unique<presentation::OffscreenRenderBackend>(
        presentation::OffscreenRenderBackendCreateInfo {.physicalDevice = *resources.device->physicalDevice,
                                                        .device = resources.device->device,
                                                        .queueFamily = selected.queueFamily,
                                                        .submissions = *resources.submissions,
                                                        .pool = *resources.memoryPool,
                                                        .performance = resources.performance.get(),
                                                        .maxLights = resources.maxLights});
    if (surface != nullptr)
    {
        if (const auto initialized = initializePresentation(resources, *surface, selected, resources.maxLights);
            !initialized)
        {
            return std::unexpected {initialized.error()};
        }
    }
    populateEngineDiagnostics(resources, selected, options);
    return {};
}

void populateEngineDiagnostics(EngineCreateInfo& resources,
                               const SelectedDevice& selected,
                               const EngineOptions& options)
{
    resources.device->info = selected.info;
    resources.features = EngineFeatures {
        .windowRenderingEnabled = resources.windowPresentation != nullptr,
        .validationEnabled = options.enableValidation,
        .performance = {.hasGpuTimestampQueries = selected.timestampValidBits != 0,
                        .isGpuTimestampSamplingEnabled = resources.performance != nullptr},
        .externalBufferExportEnabled = resources.externalInterop != nullptr,
        .externalSemaphoreExportEnabled =
            resources.externalInterop != nullptr && resources.externalInterop->supportsSemaphoreExport()
    };
}

void populateDeviceUuid(EngineCreateInfo& resources, SelectedDevice& selected)
{
    const auto properties = resources.device->physicalDevice
                                .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
    selected.info.deviceUuid = properties.get<vk::PhysicalDeviceIDProperties>().deviceUUID;
}

auto initializeSelectedDeviceResources(EngineCreateInfo& resources,
                                       const SelectedDevice& selected,
                                       DeviceResourceOptions options) -> Result<void>
{
    resources.memoryPool = resources::MemoryPool::create(resources.device->physicalDevice, resources.device->device);
    resources.submissions =
        std::make_unique<api_detail::SubmissionQueue>(resources.device->device, selected.queueFamily);
    if (const auto initialized = resources.submissions->initialize(*resources.device->physicalDevice); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    resources.uploads = std::make_unique<upload::UploadManager>(resources.device->device,
                                                                *resources.memoryPool,
                                                                *resources.submissions,
                                                                selected.queueFamily);
    if (options.externalSupport)
    {
        resources.externalInterop = std::make_unique<api_detail::ExternalBufferInterop>(
            api_detail::ExternalBufferInterop::CreateInfo {.device = resources.device->device,
                                                           .pool = *resources.memoryPool,
                                                           .submissions = *resources.submissions,
                                                           .uploads = *resources.uploads,
                                                           .queueFamily = selected.queueFamily,
                                                           .deviceUuid = selected.info.deviceUuid,
                                                           .support = std::move(*options.externalSupport)});
    }
    return {};
}

auto initializePerformance(EngineCreateInfo& resources,
                           const SelectedDevice& selected,
                           bool enableFramePerformanceSampling) -> Result<void>
{
    if (!enableFramePerformanceSampling || selected.timestampValidBits == 0)
    {
        return {};
    }
    auto performance = std::make_unique<api_detail::PerformanceManagerImpl>(
        resources.device->device,
        *resources.submissions,
        api_detail::TimestampProperties {.validBits = selected.timestampValidBits,
                                         .periodNanoseconds = selected.timestampPeriodNanoseconds});
    if (const auto initialized = performance->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    resources.performance = std::move(performance);
    return {};
}

auto initializePresentation(EngineCreateInfo& resources,
                            const WindowSurface& surface,
                            const SelectedDevice& selected,
                            std::uint32_t maxLights) -> Result<void>
{
    const auto& instanceResources = resources.device->instance;
    const auto contextSurface = instanceResources.surface ? **instanceResources.surface : vk::SurfaceKHR {};
    auto presentation = WindowPresentation::create({.physicalDevice = selected.physicalDevice,
                                                    .device = resources.device->device,
                                                    .surface = contextSurface,
                                                    .window = surface,
                                                    .queueFamily = selected.queueFamily,
                                                    .presentQueueFamily = selected.presentQueueFamily,
                                                    .maintenance = selected.maintenance,
                                                    .submissions = *resources.submissions,
                                                    .pool = resources.memoryPool.get(),
                                                    .performance = resources.performance.get(),
                                                    .maxLights = maxLights});
    if (!presentation)
    {
        return std::unexpected {presentation.error()};
    }
    resources.windowPresentation = std::move(*presentation);
    return {};
}
}
}

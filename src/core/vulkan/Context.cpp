// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DeviceSelection.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanError.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/Presentation.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchImpl.hpp"
#include "upload/UploadManager.hpp"

namespace panda
{
namespace vulkan = core::vulkan;
namespace resources = core::vulkan::resources;
namespace upload = core::vulkan::upload;

struct Context::InstanceServices
{
    [[nodiscard]] static auto create(const vk::raii::Context& loader,
                                     const WindowSurface* surface,
                                     const ContextOptions& options) -> Result<InstanceServices>;

    vulkan::InstanceExtensions extensions;
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
        vulkan::detail::enumerateVulkan<vk::LayerProperties>([&](std::uint32_t* count, vk::LayerProperties* output) {
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

auto makeInstance(const vk::raii::Context& loader,
                  const ContextOptions& options,
                  const vulkan::InstanceExtensions& extensions) -> Result<vk::raii::Instance>
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
    return vulkan::detail::checkedCreation(instance.createDebugUtilsMessengerEXT(validationMessengerCreateInfo()));
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

auto createLogicalDevice(const vk::raii::PhysicalDevice& physicalDevice, const vk::DeviceCreateInfo& info)
    -> Result<vk::raii::Device>
{
    auto device = vk::Device {};
    const auto result = (*physicalDevice).createDevice(&info, nullptr, &device, *physicalDevice.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    // Unlike leaf objects, Device construction uses the handle immediately
    // to load Vulkan functions, so check the result before creating the RAII wrapper.
    return vk::raii::Device {physicalDevice, device};
}

auto createDevice(const vk::raii::PhysicalDevice& physicalDevice,
                  const vulkan::SelectedDevice& selected,
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
    return createLogicalDevice(physicalDevice, deviceInfo);
}

auto makeDevice(const vk::raii::PhysicalDevice& physicalDevice, const vulkan::SelectedDevice& selected, bool windowed)
    -> Result<vk::raii::Device>
{
    auto maintenance = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR {.swapchainMaintenance1 = vk::True};
    auto vulkan13 = vk::PhysicalDeviceVulkan13Features {.pNext = windowed ? &maintenance : nullptr,
                                                        .synchronization2 = vk::True,
                                                        .dynamicRendering = vk::True};
    auto vulkan12 = vk::PhysicalDeviceVulkan12Features {.pNext = &vulkan13, .timelineSemaphore = vk::True};
    const auto features = vk::PhysicalDeviceFeatures2 {.pNext = &vulkan12};
    return createDevice(physicalDevice, selected, windowed, features);
}

}

auto Context::InstanceServices::create(const vk::raii::Context& loader,
                                       const WindowSurface* surface,
                                       const ContextOptions& options) -> Result<InstanceServices>
{
    auto extensions = vulkan::instanceExtensions(surface, options.enableValidation);
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

class Context::Impl
{
public:
    [[nodiscard]] auto initialize(const WindowSurface* surface, const ContextOptions& options) -> Result<void>;
    [[nodiscard]] auto getDeviceInfo() const -> const ContextDeviceInfo&;
    [[nodiscard]] auto getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&;
    [[nodiscard]] auto presentClearFrame() -> Result<FrameResult>;
    [[nodiscard]] auto createBufferResource(const BufferDesc& description)
        -> Result<std::shared_ptr<detail::BufferResource>>;
    [[nodiscard]] auto createTextureResource(const TextureDesc& description)
        -> Result<std::shared_ptr<detail::TextureResource>>;
    [[nodiscard]] auto createMeshResource(const MeshDesc& description) -> Result<std::shared_ptr<detail::MeshResource>>;
    [[nodiscard]] auto createUploadBatch() -> Result<std::unique_ptr<detail::UploadBatchImpl>>;
    [[nodiscard]] auto poll(CompletionPoint point) -> Result<bool>;
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>;
    void drain();

private:
    [[nodiscard]] auto initializeDevice(const WindowSurface* surface, bool enableGpuTimestampSampling) -> Result<void>;
    [[nodiscard]] auto ensureUploads() -> Result<void>;
    [[nodiscard]] auto collectUploads() -> Result<void>;
    void collectCompletedUploads();

    template <typename Resource, typename Description>
    [[nodiscard]] auto createResource(const Description& description) -> Result<std::shared_ptr<Resource>>
    {
        return collectUploads().and_then([&] {
            return Resource::create(_device, *_memoryPool, description);
        });
    }

    [[nodiscard]] auto initializePresentation(const WindowSurface& surface,
                                              const vulkan::SelectedDevice& selected,
                                              vk::SurfaceKHR nativeSurface) -> Result<void>;

    vk::raii::Context _loader {vkGetInstanceProcAddr};
    InstanceServices _services;
    vk::raii::PhysicalDevice _physicalDevice {nullptr};
    vk::raii::Device _device {nullptr};
    std::unique_ptr<resources::MemoryPool> _memoryPool;
    std::unique_ptr<detail::SubmissionQueue> _submissions;
    std::unique_ptr<upload::UploadManager> _uploads;
    std::unique_ptr<detail::PerformanceManagerImpl> _performanceManagerImplementation;
    std::unique_ptr<vulkan::Presentation> _presentation;
    ContextDeviceInfo _info;
};

auto Context::Impl::initialize(const WindowSurface* surface, const ContextOptions& options) -> Result<void>
{
    if (const auto checked = verifyPrerequisites(_loader, options); !checked)
    {
        return std::unexpected {checked.error()};
    }
    auto services = InstanceServices::create(_loader, surface, options);
    if (!services)
    {
        return std::unexpected {std::move(services.error())};
    }
    _services = std::move(*services);
    return initializeDevice(surface, options.enableGpuTimestampSampling);
}

auto Context::Impl::initializeDevice(const WindowSurface* surface, bool enableGpuTimestampSampling) -> Result<void>
{
    const auto nativeSurface = _services.surface ? **_services.surface : vk::SurfaceKHR {};
    auto selected = vulkan::selectDevice(*_services.instance, nativeSurface, _services.extensions);
    if (!selected)
    {
        return std::unexpected {std::move(selected.error())};
    }
    _physicalDevice = vk::raii::PhysicalDevice {_services.instance, selected->physicalDevice};
    auto createdDevice = makeDevice(_physicalDevice, *selected, surface != nullptr);
    if (!createdDevice)
    {
        return std::unexpected {std::move(createdDevice.error())};
    }
    _device = std::move(*createdDevice);
    _memoryPool = resources::MemoryPool::create(_physicalDevice, _device);
    auto performance = std::make_unique<detail::PerformanceManagerImpl>(_device,
                                                                        selected->timestampValidBits,
                                                                        selected->timestampPeriodNanoseconds,
                                                                        enableGpuTimestampSampling);
    if (const auto initialized = performance->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    _performanceManagerImplementation = std::move(performance);
    if (surface != nullptr)
    {
        const auto initialized = initializePresentation(*surface, *selected, nativeSurface);
        if (!initialized)
        {
            return std::unexpected {initialized.error()};
        }
    }
    _info = std::move(selected->info);
    return {};
}

auto Context::Impl::initializePresentation(const WindowSurface& surface,
                                           const vulkan::SelectedDevice& selected,
                                           vk::SurfaceKHR nativeSurface) -> Result<void>
{
    _submissions = std::make_unique<detail::SubmissionQueue>(_device, selected.info.queueFamily);
    auto presentation = vulkan::Presentation::create({.physicalDevice = selected.physicalDevice,
                                                      .device = _device,
                                                      .surface = nativeSurface,
                                                      .window = surface,
                                                      .deviceInfo = selected.info,
                                                      .submissions = *_submissions,
                                                      .performance = _performanceManagerImplementation.get()});
    if (!presentation)
    {
        return std::unexpected {presentation.error()};
    }
    _presentation = std::move(*presentation);
    return {};
}

auto Context::Impl::ensureUploads() -> Result<void>
{
    if (_uploads)
    {
        return {};
    }
    if (!_submissions)
    {
        auto submissions = std::make_unique<detail::SubmissionQueue>(_device, _info.queueFamily);
        const auto initialized = submissions->initialize(*_physicalDevice);
        if (!initialized)
        {
            return std::unexpected {initialized.error()};
        }
        _submissions = std::move(submissions);
    }
    _uploads = std::make_unique<upload::UploadManager>(_device, *_memoryPool, *_submissions, _info.queueFamily);
    return {};
}

auto Context::Impl::collectUploads() -> Result<void>
{
    return _uploads ? _uploads->collect() : Result<void> {};
}

void Context::Impl::collectCompletedUploads()
{
    if (_uploads)
    {
        _uploads->collectCompleted();
    }
}

auto Context::Impl::getDeviceInfo() const -> const ContextDeviceInfo&
{
    return _info;
}

auto Context::Impl::getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&
{
    return *_performanceManagerImplementation;
}

auto Context::Impl::presentClearFrame() -> Result<FrameResult>
{
    if (!_presentation)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "A window surface is required to present a frame")};
    }
    return collectUploads().and_then([&] {
        return _presentation->presentClearFrame();
    });
}

auto Context::Impl::createBufferResource(const BufferDesc& description)
    -> Result<std::shared_ptr<detail::BufferResource>>
{
    return createResource<detail::BufferResource>(description);
}

auto Context::Impl::createTextureResource(const TextureDesc& description)
    -> Result<std::shared_ptr<detail::TextureResource>>
{
    return createResource<detail::TextureResource>(description);
}

auto Context::Impl::createMeshResource(const MeshDesc& description) -> Result<std::shared_ptr<detail::MeshResource>>
{
    return createResource<detail::MeshResource>(description);
}

auto Context::Impl::createUploadBatch() -> Result<std::unique_ptr<detail::UploadBatchImpl>>
{
    return ensureUploads().transform([&] {
        return _uploads->createBatch();
    });
}

auto Context::Impl::poll(CompletionPoint point) -> Result<bool>
{
    if (!_submissions)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No submitted work belongs to this Context")};
    }
    return _submissions->poll(point).transform([&](bool completed) {
        collectCompletedUploads();
        return completed;
    });
}

auto Context::Impl::wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>
{
    if (timeout.count() < 0 || !_submissions)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait or Context")};
    }
    return _submissions->wait(point, static_cast<std::uint64_t>(timeout.count())).transform([&](WaitStatus status) {
        collectCompletedUploads();
        return status;
    });
}

void Context::Impl::drain()
{
    if (_uploads)
    {
        expect(!_uploads->hasActiveBatches(), "Context destroyed with live upload batches");
        static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
        expect(_uploads->drain(timeoutNanoseconds), "Cannot safely drain upload submissions");
    }
    if (_presentation)
    {
        expect(_presentation->drain(), "Cannot safely drain window presentation");
    }
    expect(_memoryPool->getStats().liveBytes == 0,
           "Context destroyed with live Buffer or Texture owners or Mesh storage");
}

Context::Context(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)},
      _performanceManager {_implementation->getPerformanceManagerImplementation()}
{
}

Context::Context(Context&& other) noexcept
    : _implementation {std::move(other._implementation)},
      _performanceManager {_implementation->getPerformanceManagerImplementation()}
{
}

Context::~Context() noexcept
{
    if (_implementation)
    {
        _implementation->drain();
    }
}

auto Context::create(const ContextOptions& options) -> Result<Context>
{
    return createInternal(nullptr, options);
}

auto Context::createWithSurface(const WindowSurface& surface, const ContextOptions& options) -> Result<Context>
{
    return createInternal(&surface, options);
}

auto Context::createInternal(const WindowSurface* surface, const ContextOptions& options) -> Result<Context>
{
    auto implementation = std::make_unique<Impl>();
    if (const auto initialized = implementation->initialize(surface, options); !initialized)
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
    return _implementation->getDeviceInfo();
}

auto Context::getPerformanceManager() -> PerformanceManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _performanceManager;
}

auto Context::presentClearFrame() const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _implementation->presentClearFrame();
}

auto Context::createBuffer(const BufferDesc& description) const -> Result<Buffer>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _implementation->createBufferResource(description)
        .transform([](std::shared_ptr<detail::BufferResource> resource) {
            return Buffer {std::move(resource)};
        });
}

auto Context::createTexture(const TextureDesc& description) const -> Result<Texture>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _implementation->createTextureResource(description)
        .transform([](std::shared_ptr<detail::TextureResource> resource) {
            return Texture {std::move(resource)};
        });
}

auto Context::createMesh(const MeshDesc& description) const -> Result<Mesh>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _implementation->createMeshResource(description)
        .transform([](std::shared_ptr<detail::MeshResource> resource) {
            return Mesh {std::move(resource)};
        });
}

auto Context::createUploadBatch() const -> Result<UploadBatch>
{
    if (!_implementation)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Cannot create a batch from a moved-from Context")};
    }
    return _implementation->createUploadBatch().transform([](std::unique_ptr<detail::UploadBatchImpl> batch) {
        return UploadBatch {std::move(batch)};
    });
}

auto Context::poll(CompletionPoint point) const -> Result<bool>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No submitted work belongs to this Context")};
    }
    return _implementation->poll(point);
}

auto Context::wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait or Context")};
    }
    return _implementation->wait(point, timeout);
}

}

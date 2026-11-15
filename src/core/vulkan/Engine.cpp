// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/DeviceInfo.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <utility>

#include "ContextResources.hpp"
#include "EngineCreation.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRendering.hpp"
#include "presentation/Presentation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda
{
namespace vulkan = core::vulkan;

namespace
{
template <typename T>
auto borrowIfPresent(const std::unique_ptr<T>& owner) noexcept -> std::optional<std::reference_wrapper<T>>
{
    if (!owner)
    {
        return std::nullopt;
    }
    return std::ref(*owner);
}
}

class Engine::Impl
{
public:
    explicit Impl(std::unique_ptr<vulkan::ContextResources> resources, std::uint32_t maxLights) noexcept
        : _resources {
              std::move(resources)
    },
          _resourceManager {_resources->device(), *_resources->memoryPool(), _resources->uploadManager()},
          _submissionManager {*_resources->submissions(), _resources->uploadManager()},
          _offscreenRendering {std::make_unique<vulkan::presentation::OffscreenRendering>(
              vulkan::presentation::OffscreenRenderingCreateInfo {
                  .physicalDevice = *_resources->physicalDevice(),
                  .device = _resources->device(),
                  .queueFamily = _resources->info().queueFamily,
                  .submissions = *_resources->submissions(),
                  .pool = *_resources->memoryPool(),
                  .performance = _resources->performanceManagerImplementation().get(),
                  .maxLights = maxLights})},
          _offscreenRenderer {
              OffscreenRenderer::Dependencies {.rendering = *_offscreenRendering,
                                               .pool = *_resources->memoryPool(),
                                               .uploads = _resources->uploadManager(),
                                               .performance = *_resources->performanceManagerImplementation(),
                                               .maxLights = maxLights}},
          _windowRenderer {WindowRenderer::Dependencies {.presentation = _resources->presentation().get(),
                                                         .rendering = *_offscreenRendering,
                                                         .pool = *_resources->memoryPool(),
                                                         .uploads = _resources->uploadManager(),
                                                         .performance = *_resources->performanceManagerImplementation(),
                                                         .maxLights = maxLights}}
    {
    }

    [[nodiscard]] auto getResourceManager() const noexcept -> const ResourceManager&;
    [[nodiscard]] auto getSubmissionManager() const noexcept -> const SubmissionManager&;
    [[nodiscard]] auto getOffscreenRenderer() const noexcept -> const OffscreenRenderer&;
    [[nodiscard]] auto getWindowRenderer() const noexcept
        -> std::optional<std::reference_wrapper<const WindowRenderer>>;
    [[nodiscard]] auto getDeviceInfo() const -> const DeviceInfo&;
    [[nodiscard]] auto getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&;
    [[nodiscard]] auto getExternalInterop() noexcept
        -> std::optional<std::reference_wrapper<detail::ExternalBufferInterop>>;
    void drain();

private:
    std::unique_ptr<vulkan::ContextResources> _resources;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    std::unique_ptr<vulkan::presentation::OffscreenRendering> _offscreenRendering;
    OffscreenRenderer _offscreenRenderer;
    WindowRenderer _windowRenderer;
};

auto Engine::Impl::getResourceManager() const noexcept -> const ResourceManager&
{
    return _resourceManager;
}

auto Engine::Impl::getSubmissionManager() const noexcept -> const SubmissionManager&
{
    return _submissionManager;
}

auto Engine::Impl::getOffscreenRenderer() const noexcept -> const OffscreenRenderer&
{
    return _offscreenRenderer;
}

auto Engine::Impl::getWindowRenderer() const noexcept -> std::optional<std::reference_wrapper<const WindowRenderer>>
{
    if (!_resources->presentation())
    {
        return std::nullopt;
    }
    return std::cref(_windowRenderer);
}

auto Engine::Impl::getDeviceInfo() const -> const DeviceInfo&
{
    return _resources->info();
}

auto Engine::Impl::getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&
{
    return *_resources->performanceManagerImplementation();
}

auto Engine::Impl::getExternalInterop() noexcept -> std::optional<std::reference_wrapper<detail::ExternalBufferInterop>>
{
    return borrowIfPresent(_resources->externalInterop());
}

void Engine::Impl::drain()
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    expect(!_resources->uploadManager().hasActiveBatches(), "Engine destroyed with live upload batches");
    expect(_resources->uploadManager().drain(timeoutNanoseconds), "Cannot safely drain upload submissions");
    if (_resources->presentation())
    {
        expect(_resources->presentation()->drain(), "Cannot safely drain window presentation");
        _resources->presentation().reset();
    }
    expect(_offscreenRendering->drain(), "Cannot safely drain offscreen rendering");
    _offscreenRendering.reset();
    if (_resources->externalInterop())
    {
        expect(_resources->externalInterop()->drain(timeoutNanoseconds),
               "Cannot safely drain external handoff submissions");
    }
    expect(_resources->memoryPool()->getStats().liveBytes == 0,
           "Engine destroyed with live Buffer, Texture, Mesh or OffscreenTarget owners");
}

Engine::Engine(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)},
      _performanceManager {std::ref(_implementation->getPerformanceManagerImplementation())},
      _externalBufferManager {_implementation->getExternalInterop()}
{
}

Engine::Engine(Engine&& other) noexcept
    : _implementation {std::move(other._implementation)},
      _performanceManager {_implementation
                               ? std::optional {std::ref(_implementation->getPerformanceManagerImplementation())}
                               : std::nullopt},
      _externalBufferManager {_implementation ? _implementation->getExternalInterop() : std::nullopt}
{
}

Engine::~Engine() noexcept
{
    if (_implementation)
    {
        _implementation->drain();
    }
}

auto Engine::create(const EngineOptions& options) -> Result<Engine>
{
    return createInternal(nullptr, options);
}

auto Engine::createWithSurface(const WindowSurface& surface, const EngineOptions& options) -> Result<Engine>
{
    return createInternal(&surface, options);
}

auto Engine::createInternal(const WindowSurface* surface, const EngineOptions& options) -> Result<Engine>
{
    auto resources = vulkan::engine::createResources(surface, options);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    auto implementation = std::make_unique<Impl>(std::move(*resources), options.maxLights);
    return Engine {std::move(implementation)};
}

auto Engine::isValid() const noexcept -> bool
{
    return _implementation != nullptr;
}

auto Engine::getDeviceInfo() const -> DeviceInfo
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->getDeviceInfo();
}

auto Engine::getResourceManager() const -> const ResourceManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->getResourceManager();
}

auto Engine::getSubmissionManager() const -> const SubmissionManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->getSubmissionManager();
}

auto Engine::getOffscreenRenderer() const -> const OffscreenRenderer&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->getOffscreenRenderer();
}

auto Engine::getWindowRenderer() const -> std::optional<std::reference_wrapper<const WindowRenderer>>
{
    if (!_implementation)
    {
        return std::nullopt;
    }
    return _implementation->getWindowRenderer();
}

auto Engine::getPerformanceManager() -> PerformanceManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _performanceManager;
}

auto Engine::getExternalBufferManager() -> ExternalBufferManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _externalBufferManager;
}

}

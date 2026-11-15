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
#include <panda/ExternalBufferManager.hpp>
#include <panda/OffscreenRenderer.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/ResourceManager.hpp>
#include <panda/SubmissionManager.hpp>
#include <panda/WindowRenderer.hpp>
#include <panda/WindowSurface.hpp>
#include <utility>

#include "DeviceContext.hpp"
#include "EngineCreateInfo.hpp"
#include "EngineCreation.hpp"
#include "external/ExternalBufferInterop.hpp"
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
    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;
    Impl(Impl&&) = delete;
    auto operator=(Impl&&) -> Impl& = delete;

    explicit Impl(vulkan::EngineCreateInfo resources) noexcept;
    ~Impl() noexcept;

    [[nodiscard]] auto getResourceManager() const noexcept -> const ResourceManager&;
    [[nodiscard]] auto getSubmissionManager() const noexcept -> const SubmissionManager&;
    [[nodiscard]] auto getOffscreenRenderer() const noexcept -> const OffscreenRenderer&;
    [[nodiscard]] auto getWindowRenderer() const noexcept
        -> std::optional<std::reference_wrapper<const WindowRenderer>>;
    [[nodiscard]] auto getDeviceInfo() const -> const DeviceInfo&;
    [[nodiscard]] auto getPerformanceManager() noexcept -> PerformanceManager&;
    [[nodiscard]] auto getExternalBufferManager() noexcept -> ExternalBufferManager&;
    void drain();

private:
    std::unique_ptr<vulkan::DeviceContext> _device;
    std::unique_ptr<vulkan::resources::MemoryPool> _memoryPool;
    std::unique_ptr<detail::SubmissionQueue> _submissions;
    std::unique_ptr<vulkan::upload::UploadManager> _uploads;
    std::unique_ptr<detail::ExternalBufferInterop> _externalInterop;
    std::unique_ptr<detail::PerformanceManagerImpl> _performance;
    std::unique_ptr<vulkan::presentation::OffscreenRendering> _offscreenRendering;
    std::unique_ptr<vulkan::Presentation> _presentation;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    OffscreenRenderer _offscreenRenderer;
    WindowRenderer _windowRenderer;
    PerformanceManager _performanceManager;
    ExternalBufferManager _externalBufferManager;
};

Engine::Impl::Impl(vulkan::EngineCreateInfo resources) noexcept
    : _device {
          std::move(resources.device)
},
      _memoryPool {std::move(resources.memoryPool)},
      _submissions {std::move(resources.submissions)},
      _uploads {std::move(resources.uploads)},
      _externalInterop {std::move(resources.externalInterop)},
      _performance {std::move(resources.performance)},
      _offscreenRendering {std::move(resources.offscreenRendering)},
      _presentation {std::move(resources.presentation)},
      _resourceManager {_device->device, *_memoryPool, *_uploads},
      _submissionManager {*_submissions, *_uploads},
      _offscreenRenderer {OffscreenRenderer::Dependencies {.rendering = *_offscreenRendering,
                                                           .pool = *_memoryPool,
                                                           .uploads = *_uploads,
                                                           .performance = *_performance,
                                                           .maxLights = resources.maxLights}},
      _windowRenderer {WindowRenderer::Dependencies {.presentation = _presentation.get(),
                                                     .rendering = *_offscreenRendering,
                                                     .pool = *_memoryPool,
                                                     .uploads = *_uploads,
                                                     .performance = *_performance,
                                                     .maxLights = resources.maxLights}},
      _performanceManager {*_performance}, _externalBufferManager {borrowIfPresent(_externalInterop)}
{
}

Engine::Impl::~Impl() noexcept
{
    drain();
}

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
    if (!_presentation)
    {
        return std::nullopt;
    }
    return std::cref(_windowRenderer);
}

auto Engine::Impl::getDeviceInfo() const -> const DeviceInfo&
{
    return _device->info;
}

auto Engine::Impl::getPerformanceManager() noexcept -> PerformanceManager&
{
    return _performanceManager;
}

auto Engine::Impl::getExternalBufferManager() noexcept -> ExternalBufferManager&
{
    return _externalBufferManager;
}

void Engine::Impl::drain()
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    expect(!_uploads->hasActiveBatches(), "Engine destroyed with live upload batches");
    expect(_uploads->drain(timeoutNanoseconds), "Cannot safely drain upload submissions");
    if (_presentation)
    {
        expect(_presentation->drain(), "Cannot safely drain window presentation");
        _presentation.reset();
    }
    expect(_offscreenRendering->drain(), "Cannot safely drain offscreen rendering");
    _offscreenRendering.reset();
    if (_externalInterop)
    {
        expect(_externalInterop->drain(timeoutNanoseconds), "Cannot safely drain external handoff submissions");
    }
    expect(_memoryPool->getStats().liveBytes == 0,
           "Engine destroyed with live Buffer, Texture, Mesh or OffscreenTarget owners");
}

Engine::Engine(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)}
{
}

Engine::Engine(Engine&& other) noexcept = default;

Engine::~Engine() noexcept = default;

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
    auto implementation = std::make_unique<Impl>(std::move(*resources));
    return Engine {std::move(implementation)};
}

auto Engine::isValid() const noexcept -> bool
{
    return _implementation != nullptr;
}

auto Engine::getDeviceInfo() const -> DeviceInfo
{
    return expect(_implementation, "Cannot use a moved-from Engine").getDeviceInfo();
}

auto Engine::getResourceManager() const -> const ResourceManager&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getResourceManager();
}

auto Engine::getSubmissionManager() const -> const SubmissionManager&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getSubmissionManager();
}

auto Engine::getOffscreenRenderer() const -> const OffscreenRenderer&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getOffscreenRenderer();
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
    return expect(_implementation, "Cannot use a moved-from Engine").getPerformanceManager();
}

auto Engine::getExternalBufferManager() -> ExternalBufferManager&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getExternalBufferManager();
}

}

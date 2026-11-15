// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/DeviceInfo.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <panda/WindowSurface.hpp>
#include <utility>

#include "ContextResources.hpp"
#include "EngineCreation.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRendering.hpp"
#include "presentation/Presentation.hpp"
#include "renderer/RenderViewValidation.hpp"
#include "resources/OffscreenTargetResource.hpp"
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
        : _resources {std::move(resources)},
          _resourceManager {_resources->device(), *_resources->memoryPool(), _resources->uploadManager()},
          _submissionManager {*_resources->submissions(), _resources->uploadManager()},
          _maxLights {maxLights}
    {
    }

    [[nodiscard]] auto getResourceManager() const noexcept -> const ResourceManager&;
    [[nodiscard]] auto getSubmissionManager() const noexcept -> const SubmissionManager&;
    [[nodiscard]] auto getDeviceInfo() const -> const DeviceInfo&;
    [[nodiscard]] auto hasWindowSurface() const noexcept -> bool;
    [[nodiscard]] auto getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&;
    [[nodiscard]] auto render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
        -> Result<FrameResult>;
    [[nodiscard]] auto render(const RenderView& view,
                              const OffscreenTarget& target,
                              std::optional<std::size_t>& submittedPerformanceSlot) -> Result<FrameResult>;
    [[nodiscard]] auto renderWithCapture(const RenderView& view,
                                         const OffscreenTarget& target,
                                         std::optional<std::size_t>& submittedPerformanceSlot) -> Result<FrameResult>;
    [[nodiscard]] auto readPixels(const OffscreenTarget& target, CompletionPoint completion) -> Result<ImageReadback>;
    [[nodiscard]] auto getExternalInterop() noexcept
        -> std::optional<std::reference_wrapper<detail::ExternalBufferInterop>>;
    void drain();

private:
    [[nodiscard]] auto ensureOffscreenRendering() -> Result<void>;
    [[nodiscard]] auto collectUploads() -> Result<void>;
    [[nodiscard]] auto validateCaptureTarget(const OffscreenTarget& target) const -> Result<void>;

    std::unique_ptr<vulkan::ContextResources> _resources;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    std::unique_ptr<vulkan::presentation::OffscreenRendering> _offscreenRendering;
    std::uint32_t _maxLights;
};

auto Engine::Impl::ensureOffscreenRendering() -> Result<void>
{
    if (_offscreenRendering)
    {
        return {};
    }
    auto rendering = vulkan::presentation::OffscreenRendering::create(
        {.physicalDevice = *_resources->physicalDevice(),
         .device = _resources->device(),
         .queueFamily = _resources->info().queueFamily,
         .submissions = *_resources->submissions(),
         .pool = *_resources->memoryPool(),
         .performance = _resources->performanceManagerImplementation().get(),
         .maxLights = _maxLights});
    if (!rendering)
    {
        return std::unexpected {rendering.error()};
    }
    _offscreenRendering = std::move(*rendering);
    return {};
}

auto Engine::Impl::collectUploads() -> Result<void>
{
    return _resources->uploadManager().collect();
}

auto Engine::Impl::getResourceManager() const noexcept -> const ResourceManager&
{
    return _resourceManager;
}

auto Engine::Impl::getSubmissionManager() const noexcept -> const SubmissionManager&
{
    return _submissionManager;
}

auto Engine::Impl::getDeviceInfo() const -> const DeviceInfo&
{
    return _resources->info();
}

auto Engine::Impl::hasWindowSurface() const noexcept -> bool
{
    return _resources->presentation() != nullptr;
}

auto Engine::Impl::getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&
{
    return *_resources->performanceManagerImplementation();
}

auto Engine::Impl::render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
    -> Result<FrameResult>
{
    if (const auto valid = vulkan::renderer::validateRenderView(view, _maxLights, *_resources->memoryPool()); !valid)
    {
        return std::unexpected {valid.error()};
    }
    if (!_resources->presentation())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "A window surface is required to present a frame")};
    }
    return collectUploads().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = vulkan::renderer::validateRenderViewTextures(view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        return _resources->presentation()->render(view, submittedPerformanceSlot);
    });
}

auto Engine::Impl::render(const RenderView& view,
                          const OffscreenTarget& target,
                          std::optional<std::size_t>& submittedPerformanceSlot) -> Result<FrameResult>
{
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(*_resources->memoryPool()))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    if (const auto valid = vulkan::renderer::validateRenderView(view, _maxLights, *_resources->memoryPool()); !valid)
    {
        return std::unexpected {valid.error()};
    }
    return collectUploads().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = vulkan::renderer::validateRenderViewTextures(view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        if (const auto initialized = ensureOffscreenRendering(); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
        return _offscreenRendering->render(view, target, submittedPerformanceSlot);
    });
}

auto Engine::Impl::renderWithCapture(const RenderView& view,
                                     const OffscreenTarget& target,
                                     std::optional<std::size_t>& submittedPerformanceSlot) -> Result<FrameResult>
{
    submittedPerformanceSlot.reset();
    if (const auto valid = validateCaptureTarget(target); !valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto valid = vulkan::renderer::validateRenderView(view, _maxLights, *_resources->memoryPool()); !valid)
    {
        return std::unexpected {valid.error()};
    }
    return collectUploads().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = vulkan::renderer::validateRenderViewTextures(view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        if (const auto initialized = ensureOffscreenRendering(); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
        return _resources->presentation()->renderWithCapture({.view = view,
                                                              .target = target,
                                                              .rendering = *_offscreenRendering,
                                                              .submittedPerformanceSlot = submittedPerformanceSlot});
    });
}

auto Engine::Impl::validateCaptureTarget(const OffscreenTarget& target) const -> Result<void>
{
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(*_resources->memoryPool()))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    if (!_resources->presentation())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "A window surface is required to capture a frame")};
    }
    return {};
}

auto Engine::Impl::readPixels(const OffscreenTarget& target, CompletionPoint completion) -> Result<ImageReadback>
{
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(*_resources->memoryPool()))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    if (!targetResource->isInitialized())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen target has not been rendered")};
    }
    if (const auto initialized = ensureOffscreenRendering(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return _offscreenRendering->readPixels(target, completion);
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
    if (_offscreenRendering)
    {
        expect(_offscreenRendering->drain(), "Cannot safely drain offscreen rendering");
        _offscreenRendering.reset();
    }
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
      _offscreenRenderer {*this},
      _windowRenderer {*this},
      _performanceManager {std::ref(_implementation->getPerformanceManagerImplementation())},
      _externalBufferManager {_implementation->getExternalInterop()}
{
}

Engine::Engine(Engine&& other) noexcept
    : _implementation {std::move(other._implementation)},
      _offscreenRenderer {*this},
      _windowRenderer {*this},
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
    return _offscreenRenderer;
}

auto Engine::getWindowRenderer() const -> std::optional<std::reference_wrapper<const WindowRenderer>>
{
    if (!_implementation || !_implementation->hasWindowSurface())
    {
        return std::nullopt;
    }
    return std::cref(_windowRenderer);
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

auto Engine::renderWindow(const RenderView& view) const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    auto frameStart = std::optional<std::chrono::steady_clock::time_point> {};
    if (_performanceManager.getCapabilities().isGpuTimestampSamplingEnabled)
    {
        frameStart = std::chrono::steady_clock::now();
    }
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    auto result = _implementation->render(view, submittedPerformanceSlot);
    if (frameStart && submittedPerformanceSlot)
    {
        const auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - *frameStart);
        _implementation->getPerformanceManagerImplementation().recordCpuFrameDuration(*submittedPerformanceSlot,
                                                                                      duration);
    }
    return result;
}

auto Engine::renderOffscreen(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    auto frameStart = std::optional<std::chrono::steady_clock::time_point> {};
    if (_performanceManager.getCapabilities().isGpuTimestampSamplingEnabled)
    {
        frameStart = std::chrono::steady_clock::now();
    }
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    auto result = _implementation->render(view, target, submittedPerformanceSlot);
    if (frameStart && submittedPerformanceSlot)
    {
        const auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - *frameStart);
        _implementation->getPerformanceManagerImplementation().recordCpuFrameDuration(*submittedPerformanceSlot,
                                                                                      duration);
    }
    return result;
}

auto Engine::renderWindowWithCapture(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    auto frameStart = std::optional<std::chrono::steady_clock::time_point> {};
    if (_performanceManager.getCapabilities().isGpuTimestampSamplingEnabled)
    {
        frameStart = std::chrono::steady_clock::now();
    }
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    auto result = _implementation->renderWithCapture(view, target, submittedPerformanceSlot);
    if (frameStart && submittedPerformanceSlot)
    {
        const auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - *frameStart);
        _implementation->getPerformanceManagerImplementation().recordCpuFrameDuration(*submittedPerformanceSlot,
                                                                                      duration);
    }
    return result;
}

auto Engine::readPixels(const OffscreenTarget& target, CompletionPoint completion) const -> Result<ImageReadback>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot read pixels from a moved-from Engine")};
    }
    return _implementation->readPixels(target, completion);
}

}

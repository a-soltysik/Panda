// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <panda/DeviceInfo.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/interop/ExternalBufferManager.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/rendering/WindowRenderer.hpp>
#include <panda/resources/ResourceManager.hpp>
#include <panda/submission/SubmissionManager.hpp>
#include <panda/vulkan/WindowSurface.hpp>
#include <utility>

#include "DeviceContext.hpp"
#include "EngineCreateInfo.hpp"
#include "EngineCreation.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "presentation/OffscreenRenderBackend.hpp"
#include "presentation/WindowPresentation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda
{
namespace vulkan = core::vulkan;

namespace
{
auto isValidTextureFilteringPreset(TextureFilteringPreset preset) -> bool
{
    switch (preset)
    {
    case TextureFilteringPreset::Nearest:
    case TextureFilteringPreset::Bilinear:
    case TextureFilteringPreset::Trilinear:
    case TextureFilteringPreset::Anisotropic2x:
    case TextureFilteringPreset::Anisotropic4x:
    case TextureFilteringPreset::Anisotropic8x:
    case TextureFilteringPreset::Anisotropic16x:
        return true;
    }
    return false;
}

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

    explicit Impl(vulkan::EngineCreateInfo resources);
    ~Impl() noexcept;

    [[nodiscard]] auto getResourceManager() const noexcept -> const ResourceManager&;
    [[nodiscard]] auto getSubmissionManager() const noexcept -> const SubmissionManager&;
    [[nodiscard]] auto getOffscreenRenderer() const noexcept -> const OffscreenRenderer&;
    [[nodiscard]] auto getWindowRenderer() const noexcept -> const WindowRenderer&;
    [[nodiscard]] auto getDeviceInfo() const -> const DeviceInfo&;
    [[nodiscard]] auto getFeatures() const noexcept -> EngineFeatures;
    [[nodiscard]] auto getTextureFilteringPreset() const noexcept -> TextureFilteringPreset;
    [[nodiscard]] auto setTextureFilteringPreset(TextureFilteringPreset preset) -> Result<void>;
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
    std::shared_ptr<TextureFilteringPreset> _textureFilteringPreset;
    std::unique_ptr<vulkan::presentation::OffscreenRenderBackend> _backend;
    std::unique_ptr<vulkan::WindowPresentation> _presentation;
    EngineFeatures _features;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    OffscreenRenderer _offscreenRenderer;
    std::unique_ptr<WindowRenderer> _windowRenderer;
    std::unique_ptr<PerformanceManager> _performanceManager;
    ExternalBufferManager _externalBufferManager;
};

Engine::Impl::Impl(vulkan::EngineCreateInfo resources)
    : _device {
          std::move(resources.device)
},
      _memoryPool {std::move(resources.memoryPool)},
      _submissions {std::move(resources.submissions)},
      _uploads {std::move(resources.uploads)},
      _externalInterop {std::move(resources.externalInterop)},
      _performance {std::move(resources.performance)},
      _textureFilteringPreset {std::move(resources.textureFilteringPreset)},
      _backend {std::move(resources.offscreenRenderBackend)},
      _presentation {std::move(resources.windowPresentation)},
      _features {resources.features},
      _resourceManager {_device->device, *_memoryPool, *_uploads},
      _submissionManager {*_submissions, *_uploads},
      _offscreenRenderer {OffscreenRenderer::Dependencies {.rendering = *_backend,
                                                           .pool = *_memoryPool,
                                                           .uploads = *_uploads,
                                                           .performance = _performance.get(),
                                                           .maxLights = resources.maxLights}},
      _externalBufferManager {borrowIfPresent(_externalInterop)}
{
    if (_presentation)
    {
        _windowRenderer = std::unique_ptr<WindowRenderer> {
            new WindowRenderer {WindowRenderer::Dependencies {.presentation = *_presentation,
                                                              .rendering = *_backend,
                                                              .pool = *_memoryPool,
                                                              .uploads = *_uploads,
                                                              .performance = _performance.get(),
                                                              .maxLights = resources.maxLights}}};
    }
    if (_performance)
    {
        _performanceManager = std::unique_ptr<PerformanceManager> {new PerformanceManager {*_performance}};
    }
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

auto Engine::Impl::getWindowRenderer() const noexcept -> const WindowRenderer&
{
    return expect(_windowRenderer, "A window renderer requires a windowed Engine");
}

auto Engine::Impl::getDeviceInfo() const -> const DeviceInfo&
{
    return _device->info;
}

auto Engine::Impl::getFeatures() const noexcept -> EngineFeatures
{
    return _features;
}

auto Engine::Impl::getTextureFilteringPreset() const noexcept -> TextureFilteringPreset
{
    return *_textureFilteringPreset;
}

auto Engine::Impl::setTextureFilteringPreset(TextureFilteringPreset preset) -> Result<void>
{
    if (!isValidTextureFilteringPreset(preset))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid texture filtering preset")};
    }
    *_textureFilteringPreset = preset;
    return {};
}

auto Engine::Impl::getPerformanceManager() noexcept -> PerformanceManager&
{
    return expect(_performanceManager, "Performance sampling is not enabled for this Engine");
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
        _windowRenderer.reset();
        _presentation.reset();
    }
    expect(_backend->drain(), "Cannot safely drain offscreen rendering");
    _backend.reset();
    if (_externalInterop)
    {
        expect(_externalInterop->drain(timeoutNanoseconds), "Cannot safely drain external handoff submissions");
    }
    expect(_memoryPool->getStats().liveBytes == 0,
           "Engine destroyed with live Buffer, Texture, Mesh or OffscreenTarget owners");
}

Engine::Engine(vulkan::EngineCreateInfo&& resources)
    : _implementation {std::make_unique<Impl>(std::move(resources))}
{
}

Engine::Engine(Engine&& other) noexcept = default;

Engine::~Engine() noexcept = default;

auto Engine::isValid() const noexcept -> bool
{
    return _implementation != nullptr;
}

auto Engine::getDeviceInfo() const -> DeviceInfo
{
    return expect(_implementation, "Cannot use a moved-from Engine").getDeviceInfo();
}

auto Engine::getFeatures() const -> EngineFeatures
{
    return expect(_implementation, "Cannot use a moved-from Engine").getFeatures();
}

auto Engine::getTextureFilteringPreset() const -> TextureFilteringPreset
{
    return expect(_implementation, "Cannot use a moved-from Engine").getTextureFilteringPreset();
}

auto Engine::setTextureFilteringPreset(TextureFilteringPreset preset) const -> Result<void>
{
    return expect(_implementation, "Cannot use a moved-from Engine").setTextureFilteringPreset(preset);
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

auto Engine::getWindowRenderer() const -> const WindowRenderer&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getWindowRenderer();
}

auto Engine::getPerformanceManager() -> PerformanceManager&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getPerformanceManager();
}

auto Engine::getExternalBufferManager() -> ExternalBufferManager&
{
    return expect(_implementation, "Cannot use a moved-from Engine").getExternalBufferManager();
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
    if (!isValidTextureFilteringPreset(options.textureFilteringPreset))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid texture filtering preset")};
    }
    auto resources = core::vulkan::engine::createResources(surface, options);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    return Engine {std::move(*resources)};
}

}

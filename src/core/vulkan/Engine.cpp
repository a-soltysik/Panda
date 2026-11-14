// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/DeviceInfo.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/FrameResult.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ContextResources.hpp"
#include "EngineCreation.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalContext.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRendering.hpp"
#include "presentation/Presentation.hpp"
#include "renderer/RenderViewValidation.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/OffscreenTargetResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchImpl.hpp"
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
          _maxLights {maxLights}
    {
    }

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
    [[nodiscard]] auto createBufferResource(const BufferDesc& description)
        -> Result<std::shared_ptr<detail::BufferResource>>;
    [[nodiscard]] auto getExternalContext() noexcept -> std::optional<std::reference_wrapper<detail::ExternalContext>>;
    [[nodiscard]] auto createTextureResource(const TextureDesc& description)
        -> Result<std::shared_ptr<detail::TextureResource>>;
    [[nodiscard]] auto createOffscreenTargetResource(const OffscreenTargetDesc& description)
        -> Result<std::shared_ptr<detail::OffscreenTargetResource>>;
    [[nodiscard]] auto createMeshResource(const MeshDesc& description) -> Result<std::shared_ptr<detail::MeshResource>>;
    [[nodiscard]] auto createUploadBatch() -> Result<std::unique_ptr<detail::UploadBatchImpl>>;
    [[nodiscard]] auto poll(CompletionPoint point) -> Result<bool>;
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>;
    void drain();

private:
    [[nodiscard]] auto ensureUploads() -> Result<void>;
    [[nodiscard]] auto ensureSubmissions() -> Result<void>;
    [[nodiscard]] auto ensureOffscreenRendering() -> Result<void>;
    [[nodiscard]] auto collectUploads() -> Result<void>;
    void collectCompletedUploads();
    [[nodiscard]] auto validateCaptureTarget(const OffscreenTarget& target) const -> Result<void>;

    template <typename Resource, typename Description>
    [[nodiscard]] auto createResource(const Description& description) -> Result<std::shared_ptr<Resource>>
    {
        return collectUploads().and_then([&] {
            return Resource::create(_resources->device(), *_resources->memoryPool(), description);
        });
    }

    std::unique_ptr<vulkan::ContextResources> _resources;
    std::unique_ptr<vulkan::presentation::OffscreenRendering> _offscreenRendering;
    std::uint32_t _maxLights;
};

auto Engine::Impl::ensureUploads() -> Result<void>
{
    if (_resources->uploads())
    {
        return {};
    }
    if (const auto submissions = ensureSubmissions(); !submissions)
    {
        return submissions;
    }
    _resources->uploads().emplace(_resources->device(),
                                  *_resources->memoryPool(),
                                  *_resources->submissions(),
                                  _resources->info().queueFamily);
    return {};
}

auto Engine::Impl::ensureSubmissions() -> Result<void>
{
    if (_resources->submissions())
    {
        return {};
    }
    if (!_resources->submissions())
    {
        auto submissions =
            std::make_unique<detail::SubmissionQueue>(_resources->device(), _resources->info().queueFamily);
        const auto initialized = submissions->initialize(*_resources->physicalDevice());
        if (!initialized)
        {
            return std::unexpected {initialized.error()};
        }
        _resources->submissions() = std::move(submissions);
    }
    return {};
}

auto Engine::Impl::ensureOffscreenRendering() -> Result<void>
{
    if (_offscreenRendering)
    {
        return {};
    }
    if (const auto submissions = ensureSubmissions(); !submissions)
    {
        return submissions;
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
    auto& uploads = _resources->uploads();
    return uploads ? uploads->collect() : Result<void> {};
}

void Engine::Impl::collectCompletedUploads()
{
    auto& uploads = _resources->uploads();
    if (uploads)
    {
        uploads->collectCompleted();
    }
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

auto Engine::Impl::createBufferResource(const BufferDesc& description)
    -> Result<std::shared_ptr<detail::BufferResource>>
{
    return createResource<detail::BufferResource>(description);
}

auto Engine::Impl::getExternalContext() noexcept -> std::optional<std::reference_wrapper<detail::ExternalContext>>
{
    return borrowIfPresent(_resources->externalContext());
}

auto Engine::Impl::createTextureResource(const TextureDesc& description)
    -> Result<std::shared_ptr<detail::TextureResource>>
{
    return createResource<detail::TextureResource>(description);
}

auto Engine::Impl::createOffscreenTargetResource(const OffscreenTargetDesc& description)
    -> Result<std::shared_ptr<detail::OffscreenTargetResource>>
{
    return createResource<detail::OffscreenTargetResource>(description);
}

auto Engine::Impl::createMeshResource(const MeshDesc& description) -> Result<std::shared_ptr<detail::MeshResource>>
{
    return createResource<detail::MeshResource>(description);
}

auto Engine::Impl::createUploadBatch() -> Result<std::unique_ptr<detail::UploadBatchImpl>>
{
    return ensureUploads().transform([&] {
        return _resources->uploads()->createBatch();
    });
}

auto Engine::Impl::poll(CompletionPoint point) -> Result<bool>
{
    if (!_resources->submissions())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No submitted work belongs to this Engine")};
    }
    return _resources->submissions()->poll(point).transform([&](bool completed) {
        collectCompletedUploads();
        return completed;
    });
}

auto Engine::Impl::wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>
{
    if (timeout.count() < 0 || !_resources->submissions())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait or Engine")};
    }
    return _resources->submissions()
        ->wait(point, static_cast<std::uint64_t>(timeout.count()))
        .transform([&](WaitStatus status) {
            collectCompletedUploads();
            return status;
        });
}

void Engine::Impl::drain()
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    auto& uploads = _resources->uploads();
    if (uploads)
    {
        expect(!uploads->hasActiveBatches(), "Engine destroyed with live upload batches");
        expect(uploads->drain(timeoutNanoseconds), "Cannot safely drain upload submissions");
    }
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
    if (_resources->externalContext())
    {
        expect(_resources->externalContext()->drain(timeoutNanoseconds),
               "Cannot safely drain external handoff submissions");
    }
    expect(_resources->memoryPool()->getStats().liveBytes == 0,
           "Engine destroyed with live Buffer, Texture, Mesh or OffscreenTarget owners");
}

Engine::Engine(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)},
      _resourceManager {*this},
      _submissionManager {*this},
      _offscreenRenderer {*this},
      _windowRenderer {*this},
      _performanceManager {std::ref(_implementation->getPerformanceManagerImplementation())},
      _externalBufferManager {_implementation->getExternalContext()}
{
}

Engine::Engine(Engine&& other) noexcept
    : _implementation {std::move(other._implementation)},
      _resourceManager {*this},
      _submissionManager {*this},
      _offscreenRenderer {*this},
      _windowRenderer {*this},
      _performanceManager {_implementation
                               ? std::optional {std::ref(_implementation->getPerformanceManagerImplementation())}
                               : std::nullopt},
      _externalBufferManager {_implementation ? _implementation->getExternalContext() : std::nullopt}
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
    return _resourceManager;
}

auto Engine::getSubmissionManager() const -> const SubmissionManager&
{
    return _submissionManager;
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

auto Engine::createBuffer(const BufferDesc& description) const -> Result<Buffer>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->createBufferResource(description)
        .transform([](std::shared_ptr<detail::BufferResource> resource) {
            return Buffer {std::move(resource)};
        });
}

auto Engine::createTexture(const TextureDesc& description) const -> Result<Texture>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->createTextureResource(description)
        .transform([](std::shared_ptr<detail::TextureResource> resource) {
            return Texture {std::move(resource)};
        });
}

auto Engine::createOffscreenTarget(const OffscreenTargetDesc& description) const -> Result<OffscreenTarget>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->createOffscreenTargetResource(description)
        .transform([](std::shared_ptr<detail::OffscreenTargetResource> resource) {
            return OffscreenTarget {std::move(resource)};
        });
}

auto Engine::createMesh(const MeshDesc& description) const -> Result<Mesh>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Engine");
    return _implementation->createMeshResource(description)
        .transform([](std::shared_ptr<detail::MeshResource> resource) {
            return Mesh {std::move(resource)};
        });
}

auto Engine::createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>
{
    if (geometry.indices.valueless_by_exception() ||
        geometry.vertices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Geometry cannot be represented by a Mesh")};
    }
    const auto [indexCount, indexType] = std::visit(
        []<typename Index>(const std::vector<Index>& indices) {
            const auto type = std::is_same_v<Index, std::uint16_t> ? MeshIndexType::UInt16 : MeshIndexType::UInt32;
            return std::pair {indices.size(), type};
        },
        geometry.indices);
    if (indexCount > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Geometry has too many indices")};
    }
    auto indexDescription = std::optional<MeshIndexDesc> {};
    if (indexCount != 0)
    {
        indexDescription = MeshIndexDesc {.count = static_cast<std::uint32_t>(indexCount), .type = indexType};
    }
    return createMesh({.vertexCount = static_cast<std::uint32_t>(geometry.vertices.size()),
                       .vertexStride = standardMeshVertexStrideBytes,
                       .indices = indexDescription,
                       .attributes = std::ranges::to<std::vector<VertexAttribute>>(standardMeshVertexAttributes)});
}

auto Engine::createUploadBatch() const -> Result<UploadBatch>
{
    if (!_implementation)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Cannot create a batch from a moved-from Engine")};
    }
    return _implementation->createUploadBatch().transform([](std::unique_ptr<detail::UploadBatchImpl> batch) {
        return UploadBatch {std::move(batch)};
    });
}

auto Engine::poll(CompletionPoint point) const -> Result<bool>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No submitted work belongs to this Engine")};
    }
    return _implementation->poll(point);
}

auto Engine::wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>
{
    if (!_implementation)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait or Engine")};
    }
    return _implementation->wait(point, timeout);
}

}

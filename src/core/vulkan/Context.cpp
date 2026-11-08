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
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ContextCreation.hpp"
#include "ContextResources.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalContext.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/Presentation.hpp"
#include "renderer/RenderViewValidation.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
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

class Context::Impl
{
public:
    explicit Impl(std::unique_ptr<vulkan::ContextResources> resources) noexcept
        : _resources {std::move(resources)}
    {
    }

    [[nodiscard]] auto getDeviceInfo() const -> const ContextDeviceInfo&;
    [[nodiscard]] auto getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&;
    [[nodiscard]] auto render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
        -> Result<FrameResult>;
    [[nodiscard]] auto createBufferResource(const BufferDesc& description)
        -> Result<std::shared_ptr<detail::BufferResource>>;
    [[nodiscard]] auto getExternalContext() noexcept -> std::optional<std::reference_wrapper<detail::ExternalContext>>;
    [[nodiscard]] auto createTextureResource(const TextureDesc& description)
        -> Result<std::shared_ptr<detail::TextureResource>>;
    [[nodiscard]] auto createMeshResource(const MeshDesc& description) -> Result<std::shared_ptr<detail::MeshResource>>;
    [[nodiscard]] auto createUploadBatch() -> Result<std::unique_ptr<detail::UploadBatchImpl>>;
    [[nodiscard]] auto poll(CompletionPoint point) -> Result<bool>;
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>;
    void drain();

private:
    [[nodiscard]] auto ensureUploads() -> Result<void>;
    [[nodiscard]] auto collectUploads() -> Result<void>;
    void collectCompletedUploads();

    template <typename Resource, typename Description>
    [[nodiscard]] auto createResource(const Description& description) -> Result<std::shared_ptr<Resource>>
    {
        return collectUploads().and_then([&] {
            return Resource::create(_resources->device(), *_resources->memoryPool(), description);
        });
    }

    std::unique_ptr<vulkan::ContextResources> _resources;
};

auto Context::Impl::ensureUploads() -> Result<void>
{
    if (_resources->uploads())
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
    _resources->uploads().emplace(_resources->device(),
                                  *_resources->memoryPool(),
                                  *_resources->submissions(),
                                  _resources->info().queueFamily);
    return {};
}

auto Context::Impl::collectUploads() -> Result<void>
{
    auto& uploads = _resources->uploads();
    return uploads ? uploads->collect() : Result<void> {};
}

void Context::Impl::collectCompletedUploads()
{
    auto& uploads = _resources->uploads();
    if (uploads)
    {
        uploads->collectCompleted();
    }
}

auto Context::Impl::getDeviceInfo() const -> const ContextDeviceInfo&
{
    return _resources->info();
}

auto Context::Impl::getPerformanceManagerImplementation() noexcept -> detail::PerformanceManagerImpl&
{
    return *_resources->performanceManagerImplementation();
}

auto Context::Impl::render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
    -> Result<FrameResult>
{
    if (const auto valid = vulkan::renderer::validateRenderViewData(view); !valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto valid = vulkan::renderer::validateRenderViewMeshes(view, *_resources->memoryPool()); !valid)
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

auto Context::Impl::createBufferResource(const BufferDesc& description)
    -> Result<std::shared_ptr<detail::BufferResource>>
{
    return createResource<detail::BufferResource>(description);
}

auto Context::Impl::getExternalContext() noexcept -> std::optional<std::reference_wrapper<detail::ExternalContext>>
{
    return borrowIfPresent(_resources->externalContext());
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
        return _resources->uploads()->createBatch();
    });
}

auto Context::Impl::poll(CompletionPoint point) -> Result<bool>
{
    if (!_resources->submissions())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "No submitted work belongs to this Context")};
    }
    return _resources->submissions()->poll(point).transform([&](bool completed) {
        collectCompletedUploads();
        return completed;
    });
}

auto Context::Impl::wait(CompletionPoint point, std::chrono::nanoseconds timeout) -> Result<WaitStatus>
{
    if (timeout.count() < 0 || !_resources->submissions())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid completion wait or Context")};
    }
    return _resources->submissions()
        ->wait(point, static_cast<std::uint64_t>(timeout.count()))
        .transform([&](WaitStatus status) {
            collectCompletedUploads();
            return status;
        });
}

void Context::Impl::drain()
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    auto& uploads = _resources->uploads();
    if (uploads)
    {
        expect(!uploads->hasActiveBatches(), "Context destroyed with live upload batches");
        expect(uploads->drain(timeoutNanoseconds), "Cannot safely drain upload submissions");
    }
    if (_resources->presentation())
    {
        expect(_resources->presentation()->drain(), "Cannot safely drain window presentation");
        _resources->presentation().reset();
    }
    if (_resources->externalContext())
    {
        expect(_resources->externalContext()->drain(timeoutNanoseconds),
               "Cannot safely drain external handoff submissions");
    }
    expect(_resources->memoryPool()->getStats().liveBytes == 0,
           "Context destroyed with live Buffer or Texture owners or Mesh storage");
}

Context::Context(std::unique_ptr<Impl> implementation) noexcept
    : _implementation {std::move(implementation)},
      _performanceManager {std::ref(_implementation->getPerformanceManagerImplementation())},
      _externalBufferManager {_implementation->getExternalContext()}
{
}

Context::Context(Context&& other) noexcept
    : _implementation {std::move(other._implementation)},
      _performanceManager {_implementation
                               ? std::optional {std::ref(_implementation->getPerformanceManagerImplementation())}
                               : std::nullopt},
      _externalBufferManager {_implementation ? _implementation->getExternalContext() : std::nullopt}
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
    auto resources = vulkan::context::createResources(surface, options);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    auto implementation = std::make_unique<Impl>(std::move(*resources));
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

auto Context::getExternalBufferManager() -> ExternalBufferManager&
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
    return _externalBufferManager;
}

auto Context::render(const RenderView& view) const -> Result<FrameResult>
{
    expect(_implementation != nullptr, "Cannot use a moved-from Context");
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

auto Context::createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>
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

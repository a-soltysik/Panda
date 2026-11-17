// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/rendering/WindowRenderer.hpp>

#include "vulkan/performance/PerformanceManagerImpl.hpp"
#include "vulkan/presentation/OffscreenRenderBackend.hpp"
#include "vulkan/presentation/WindowPresentation.hpp"
#include "vulkan/renderer/RenderViewValidation.hpp"
#include "vulkan/resources/OffscreenTargetResource.hpp"
#include "vulkan/upload/UploadManager.hpp"

namespace panda
{
namespace
{
auto startCpuFrameTimer(const detail::PerformanceManagerImpl* performance)
    -> std::optional<std::chrono::steady_clock::time_point>
{
    if (performance != nullptr)
    {
        return std::chrono::steady_clock::now();
    }
    return std::nullopt;
}

void recordCpuFrameDuration(detail::PerformanceManagerImpl* performance,
                            const std::optional<std::chrono::steady_clock::time_point>& frameStart,
                            const std::optional<std::size_t>& submittedPerformanceSlot)
{
    if (performance != nullptr && frameStart && submittedPerformanceSlot)
    {
        const auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - *frameStart);
        performance->recordCpuFrameDuration(*submittedPerformanceSlot, duration);
    }
}

auto validateCaptureTarget(const OffscreenTarget& target, const core::vulkan::resources::MemoryPool& pool)
    -> Result<void>
{
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(pool))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    return {};
}
}

WindowRenderer::WindowRenderer(Dependencies dependencies) noexcept
    : _presentation {dependencies.presentation},
      _rendering {dependencies.rendering},
      _pool {dependencies.pool},
      _uploads {dependencies.uploads},
      _performance {dependencies.performance},
      _maxLights {dependencies.maxLights}
{
}

auto WindowRenderer::render(const RenderView& view) const -> Result<FrameResult>
{
    const auto frameStart = startCpuFrameTimer(_performance);
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    if (const auto valid = core::vulkan::renderer::validateRenderView(view, _maxLights, _pool); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto result = renderWithUploads(view, submittedPerformanceSlot);
    recordCpuFrameDuration(_performance, frameStart, submittedPerformanceSlot);
    return result;
}

auto WindowRenderer::renderWithUploads(const RenderView& view,
                                       std::optional<std::size_t>& submittedPerformanceSlot) const
    -> Result<FrameResult>
{
    return _uploads.collect().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = core::vulkan::renderer::validateRenderViewTextures(view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        return _presentation.render(view, submittedPerformanceSlot);
    });
}

auto WindowRenderer::renderWithCapture(const RenderView& view, const OffscreenTarget& target) const
    -> Result<FrameResult>
{
    const auto frameStart = startCpuFrameTimer(_performance);
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    if (const auto valid = validateCaptureTarget(target, _pool); !valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto valid = core::vulkan::renderer::validateRenderView(view, _maxLights, _pool); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto result =
        captureWithUploads(core::vulkan::CaptureFrameRequest {.view = view,
                                                              .target = target,
                                                              .rendering = _rendering,
                                                              .submittedPerformanceSlot = submittedPerformanceSlot});
    recordCpuFrameDuration(_performance, frameStart, submittedPerformanceSlot);
    return result;
}

auto WindowRenderer::captureWithUploads(core::vulkan::CaptureFrameRequest request) const -> Result<FrameResult>
{
    return _uploads.collect().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = core::vulkan::renderer::validateRenderViewTextures(request.view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        if (const auto initialized = request.rendering.initialize(); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
        return _presentation.renderWithCapture(request);
    });
}
}

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <panda/Error.hpp>
#include <panda/OffscreenRenderer.hpp>

#include "vulkan/performance/PerformanceManagerImpl.hpp"
#include "vulkan/presentation/OffscreenRenderBackend.hpp"
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
}

OffscreenRenderer::OffscreenRenderer(Dependencies dependencies) noexcept
    : _rendering {dependencies.rendering},
      _pool {dependencies.pool},
      _uploads {dependencies.uploads},
      _performance {dependencies.performance},
      _maxLights {dependencies.maxLights}
{
}

auto OffscreenRenderer::render(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>
{
    const auto frameStart = startCpuFrameTimer(_performance);
    auto submittedPerformanceSlot = std::optional<std::size_t> {};
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(_pool))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    if (const auto valid = core::vulkan::renderer::validateRenderView(view, _maxLights, _pool); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto result = renderWithUploads(view, target, submittedPerformanceSlot);
    recordCpuFrameDuration(_performance, frameStart, submittedPerformanceSlot);
    return result;
}

auto OffscreenRenderer::renderWithUploads(const RenderView& view,
                                          const OffscreenTarget& target,
                                          std::optional<std::size_t>& submittedPerformanceSlot) const
    -> Result<FrameResult>
{
    return _uploads.collect().and_then([&]() -> Result<FrameResult> {
        if (const auto valid = core::vulkan::renderer::validateRenderViewTextures(view); !valid)
        {
            return std::unexpected {valid.error()};
        }
        if (const auto initialized = _rendering.initialize(); !initialized)
        {
            return std::unexpected {initialized.error()};
        }
        return _rendering.render(view, target, submittedPerformanceSlot);
    });
}

auto OffscreenRenderer::readPixels(const OffscreenTarget& target, CompletionPoint completion) const
    -> Result<ImageReadback>
{
    const auto& targetResource = detail::OffscreenTargetResource::getResource(target);
    if (!targetResource || !targetResource->belongsTo(_pool))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Offscreen target does not belong to this Engine")};
    }
    if (!targetResource->isInitialized())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Offscreen target has not been rendered")};
    }
    if (const auto initialized = _rendering.initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return _rendering.readPixels(target, completion);
}
}

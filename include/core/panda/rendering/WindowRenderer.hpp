#pragma once

/// @file
/// Engine-owned renderer for a window surface.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>             // IWYU pragma: export
#include <panda/rendering/FrameResult.hpp>  // IWYU pragma: export
#include <panda/rendering/RenderView.hpp>   // IWYU pragma: export
#include <panda/rendering/SampleCount.hpp>
#include <panda/resources/OffscreenTarget.hpp>  // IWYU pragma: export
#include <vector>

namespace panda
{
namespace core::vulkan
{
class WindowPresentation;
struct CaptureFrameRequest;

namespace presentation
{
class OffscreenRenderBackend;
}

namespace resources
{
class MemoryPool;
}

namespace upload
{
class UploadManager;
}
}

namespace detail
{
class PerformanceManagerImpl;
}

/// @brief Renders application views to the window associated with an Engine.
/// This facade is available only when Engine was created with a WindowSurface.
/// It is borrowed from that Engine and its address remains stable when Engine moves.
class WindowRenderer final
{
public:
    WindowRenderer(const WindowRenderer&) = delete;
    auto operator=(const WindowRenderer&) -> WindowRenderer& = delete;
    WindowRenderer(WindowRenderer&&) = delete;
    auto operator=(WindowRenderer&&) -> WindowRenderer& = delete;
    ~WindowRenderer() = default;

    /// @brief Queries the presentation modes Panda supports on the current surface.
    /// @return Supported modes or a surface/backend error. May allocate.
    [[nodiscard]] auto getSupportedPresentationModes() const -> Result<std::vector<EngineOptions::PresentationMode>>;

    /// @brief Returns the last accepted request, initially from EngineOptions.
    [[nodiscard]] auto getRequestedPresentationMode() const noexcept -> EngineOptions::PresentationMode;

    /// @brief Returns the current swapchain's mode, or no value while there is no swapchain.
    [[nodiscard]] auto getEffectivePresentationMode() const noexcept -> std::optional<EngineOptions::PresentationMode>;

    /// @brief Requests a mode for the next render call with a nonzero framebuffer.
    /// @param mode Presentation policy to validate against the current surface.
    /// @return Success if queued, Unsupported if unavailable, or a validation/backend error.
    /// A rejected request leaves the previous request intact. Applying a request may block
    /// during swapchain recreation and may fail in the render call.
    [[nodiscard]] auto setPresentationMode(EngineOptions::PresentationMode mode) const -> Result<void>;

    /// @brief Queries mesh sample counts supported for the current swapchain's color format.
    /// @return Supported counts or an error. Returns Unsupported before the first swapchain exists.
    /// For a capture frame, use the offscreen renderer's query instead.
    [[nodiscard]] auto getSupportedSampleCounts() const -> Result<std::vector<SampleCount>>;

    /// @brief Renders and presents one application-supplied view.
    /// @param view Transient render data whose resources are retained through GPU completion.
    /// @return Rendered when submission and presentation request succeed without
    /// recreation; otherwise a non-rendered result or a validation/backend/window error.
    /// A valid completion token means submitted work, not completed presentation.
    [[nodiscard]] auto render(const RenderView& view) const -> Result<FrameResult>;

    /// @brief Renders a view once, presents it, and copies the same color output to a target.
    /// @param view Transient render data used for both presentation and capture.
    /// @param target Single-sample RGBA8 sRGB target created by this Engine; its extent may differ from the window.
    /// @return Checked frame result or a validation/backend/window error. The target
    /// and view resources are retained through GPU completion.
    [[nodiscard]] auto renderWithCapture(const RenderView& view, const OffscreenTarget& target) const
        -> Result<FrameResult>;

private:
    friend class Engine;

    struct Dependencies final
    {
        core::vulkan::WindowPresentation& presentation;
        core::vulkan::presentation::OffscreenRenderBackend& rendering;
        core::vulkan::resources::MemoryPool& pool;
        core::vulkan::upload::UploadManager& uploads;
        detail::PerformanceManagerImpl* performance;
        std::uint32_t maxLights;
    };

    explicit WindowRenderer(Dependencies dependencies) noexcept;
    [[nodiscard]] auto renderWithUploads(const RenderView& view,
                                         std::optional<std::size_t>& submittedPerformanceSlot) const
        -> Result<FrameResult>;
    [[nodiscard]] auto captureWithUploads(core::vulkan::CaptureFrameRequest request) const -> Result<FrameResult>;

    core::vulkan::WindowPresentation& _presentation;
    core::vulkan::presentation::OffscreenRenderBackend& _rendering;
    core::vulkan::resources::MemoryPool& _pool;
    core::vulkan::upload::UploadManager& _uploads;
    detail::PerformanceManagerImpl* _performance;
    std::uint32_t _maxLights;
};
}

#pragma once

/// @file
/// Engine-owned renderer for a window surface.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <panda/Error.hpp>            // IWYU pragma: export
#include <panda/FrameResult.hpp>      // IWYU pragma: export
#include <panda/OffscreenTarget.hpp>  // IWYU pragma: export
#include <panda/RenderView.hpp>       // IWYU pragma: export

namespace panda
{
namespace core::vulkan
{
class Presentation;
struct CaptureFrameRequest;

namespace presentation
{
class OffscreenRendering;
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

/// @brief Renders application views to the window associated with this Engine.
/// This facade is available only when the Engine has a window surface. It is
/// borrowed from that Engine and its address remains stable when Engine is moved.
class WindowRenderer final
{
public:
    WindowRenderer(const WindowRenderer&) = delete;
    auto operator=(const WindowRenderer&) -> WindowRenderer& = delete;
    WindowRenderer(WindowRenderer&&) = delete;
    auto operator=(WindowRenderer&&) -> WindowRenderer& = delete;
    ~WindowRenderer() = default;

    /// @brief Renders and presents one application-supplied view.
    /// @param view Transient render data whose resources are retained through GPU completion.
    /// @return Checked frame result or a validation/backend/window error. A valid
    /// completion token means submitted work, not completed presentation.
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
        core::vulkan::Presentation* presentation;
        core::vulkan::presentation::OffscreenRendering& rendering;
        core::vulkan::resources::MemoryPool& pool;
        core::vulkan::upload::UploadManager& uploads;
        detail::PerformanceManagerImpl& performance;
        std::uint32_t maxLights;
    };

    explicit WindowRenderer(Dependencies dependencies) noexcept;
    [[nodiscard]] auto renderWithUploads(const RenderView& view,
                                         std::optional<std::size_t>& submittedPerformanceSlot) const
        -> Result<FrameResult>;
    [[nodiscard]] auto captureWithUploads(core::vulkan::CaptureFrameRequest request) const -> Result<FrameResult>;

    core::vulkan::Presentation* _presentation;
    core::vulkan::presentation::OffscreenRendering& _rendering;
    core::vulkan::resources::MemoryPool& _pool;
    core::vulkan::upload::UploadManager& _uploads;
    detail::PerformanceManagerImpl& _performance;
    std::uint32_t _maxLights;
};
}

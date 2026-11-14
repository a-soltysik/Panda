#pragma once

/// @file
/// Engine-owned renderer for a window surface.

#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
class Engine;

/// @brief Renders application views to the window associated with this Engine.
/// This facade is available only when the Engine has a window surface and is
/// borrowed from that Engine.
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

    explicit WindowRenderer(const Engine& context) noexcept;

    const Engine* _engine;
};
}

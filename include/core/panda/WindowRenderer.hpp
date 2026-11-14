#pragma once

/// @file
/// Context-owned renderer for a window surface.

#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
class Context;

/// @brief Renders application views to the window associated with this Context.
/// This facade is available only when the Context has a window surface and is
/// borrowed from that Context.
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
    /// @param target Single-sample RGBA8 sRGB target created by this Context; its extent may differ from the window.
    /// @return Checked frame result or a validation/backend/window error. The target
    /// and view resources are retained through GPU completion.
    [[nodiscard]] auto renderWithCapture(const RenderView& view, const OffscreenTarget& target) const
        -> Result<FrameResult>;

private:
    friend class Context;

    explicit WindowRenderer(const Context& context) noexcept;

    const Context* _context;
};
}

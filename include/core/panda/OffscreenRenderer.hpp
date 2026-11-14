#pragma once

/// @file
/// Context-owned renderer for explicit offscreen targets.

#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
class Context;

/// @brief Renders views to an explicit offscreen target.
/// This facade is borrowed from Context and must not outlive it. Calls are serialized
/// with other operations on the owning Context. Targets may vary between calls.
class OffscreenRenderer final
{
public:
    OffscreenRenderer(const OffscreenRenderer&) = delete;
    auto operator=(const OffscreenRenderer&) -> OffscreenRenderer& = delete;
    OffscreenRenderer(OffscreenRenderer&&) = delete;
    auto operator=(OffscreenRenderer&&) -> OffscreenRenderer& = delete;
    ~OffscreenRenderer() = default;

    /// @brief Renders one application-supplied view without presenting a frame.
    /// @param view Transient render data whose resources are retained through GPU completion.
    /// @param target Single-sample RGBA8 sRGB target created by this Context.
    /// @return A Rendered result with completion for the submitted GPU work, or a validation/backend error.
    /// The target and view resources are retained through GPU completion.
    [[nodiscard]] auto render(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>;

    /// @brief Copies a rendered target into tightly packed top-to-bottom RGBA bytes and waits for the copy.
    /// @param target Target whose latest completed render is copied; it must belong to this Context.
    /// @param completion A Context completion point covering the target's latest render.
    /// @return RGBA8 pixels; RGB is sRGB encoded and alpha is linear UNORM.
    /// Returns InvalidArgument for a foreign target, stale/foreign completion point or target not yet rendered.
    [[nodiscard]] auto readPixels(const OffscreenTarget& target, CompletionPoint completion) const
        -> Result<ImageReadback>;

private:
    friend class Context;

    explicit OffscreenRenderer(const Context& context) noexcept;

    const Context* _context;
};
}

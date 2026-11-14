#pragma once

/// @file
/// Engine-owned renderer for explicit offscreen targets.

#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
class Engine;

/// @brief Renders views to an explicit offscreen target.
/// This facade is borrowed from Engine and must not outlive it. Calls are serialized
/// with other operations on the owning Engine. Targets may vary between calls.
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
    /// @param target Single-sample RGBA8 sRGB target created by this Engine.
    /// @return A Rendered result with completion for the submitted GPU work, or a validation/backend error.
    /// The target and view resources are retained through GPU completion.
    [[nodiscard]] auto render(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>;

    /// @brief Copies a rendered target into tightly packed top-to-bottom RGBA bytes and waits for the copy.
    /// @param target Target whose latest completed render is copied; it must belong to this Engine.
    /// @param completion An Engine completion point covering the target's latest render.
    /// @return RGBA8 pixels; RGB is sRGB encoded and alpha is linear UNORM.
    /// Returns InvalidArgument for a foreign target, stale/foreign completion point or target not yet rendered.
    [[nodiscard]] auto readPixels(const OffscreenTarget& target, CompletionPoint completion) const
        -> Result<ImageReadback>;

private:
    friend class Engine;

    explicit OffscreenRenderer(const Engine& context) noexcept;

    const Engine* _engine;
};
}

#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <panda/WindowRenderer.hpp>

namespace panda
{
WindowRenderer::WindowRenderer(const Context& context) noexcept
    : _context {&context}
{
}

auto WindowRenderer::render(const RenderView& view) const -> Result<FrameResult>
{
    return _context->renderWindow(view);
}

auto WindowRenderer::renderWithCapture(const RenderView& view, const OffscreenTarget& target) const
    -> Result<FrameResult>
{
    return _context->renderWindowWithCapture(view, target);
}
}

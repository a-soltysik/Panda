#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenRenderer.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
OffscreenRenderer::OffscreenRenderer(const Context& context) noexcept
    : _context {&context}
{
}

auto OffscreenRenderer::render(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>
{
    return _context->renderOffscreen(view, target);
}

auto OffscreenRenderer::readPixels(const OffscreenTarget& target, CompletionPoint completion) const
    -> Result<ImageReadback>
{
    return _context->readPixels(target, completion);
}
}

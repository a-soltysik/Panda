#include <panda/CompletionPoint.hpp>
#include <panda/Engine.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenRenderer.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

namespace panda
{
OffscreenRenderer::OffscreenRenderer(const Engine& context) noexcept
    : _engine {&context}
{
}

auto OffscreenRenderer::render(const RenderView& view, const OffscreenTarget& target) const -> Result<FrameResult>
{
    return _engine->renderOffscreen(view, target);
}

auto OffscreenRenderer::readPixels(const OffscreenTarget& target, CompletionPoint completion) const
    -> Result<ImageReadback>
{
    return _engine->readPixels(target, completion);
}
}

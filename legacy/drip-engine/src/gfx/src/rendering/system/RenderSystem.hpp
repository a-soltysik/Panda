#pragma once

#include "rendering/FrameInfo.hpp"

namespace drip::gfx
{

class RenderSystem
{
public:
    RenderSystem() = default;
    RenderSystem(const RenderSystem&) = delete;
    auto operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) = delete;
    auto operator=(RenderSystem&&) = delete;
    virtual ~RenderSystem() noexcept = default;

    virtual void render(const FrameInfo& frameInfo) = 0;
};

}

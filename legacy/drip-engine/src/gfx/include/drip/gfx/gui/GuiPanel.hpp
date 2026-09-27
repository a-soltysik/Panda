#pragma once

namespace drip::gfx
{
class GuiPanel
{
public:
    virtual ~GuiPanel() = default;
    virtual void render() = 0;
};
}

#pragma once
#include <memory>
#include <vector>

#include "GuiPanel.hpp"

namespace drip::gfx
{

class GuiRenderSystem;

class GuiManager
{
    friend class GuiRenderSystem;

public:
    template <typename Panel, typename... Args>
    void addPanel(Args&&... args)
    {
        _panels.emplace_back(std::make_unique<Panel>(args...));
    }

private:
    void render();
    std::vector<std::unique_ptr<GuiPanel>> _panels;
};

}

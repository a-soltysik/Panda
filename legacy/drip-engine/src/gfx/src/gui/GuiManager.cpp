#include "drip/gfx/gui/GuiManager.hpp"

#include <algorithm>

#include "drip/gfx/gui/GuiPanel.hpp"

namespace drip::gfx
{

void GuiManager::render()
{
    std::ranges::for_each(_panels, &GuiPanel::render);
}

}

#include "GuiRenderSystem.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include "drip/gfx/gui/GuiManager.hpp"
#include "rendering/FrameInfo.hpp"

namespace drip::gfx
{

GuiRenderSystem::GuiRenderSystem(GuiManager& guiManager)
    : _guiManager(guiManager)
{
}

void GuiRenderSystem::render(const FrameInfo& frameInfo)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    _guiManager.render();

    ImGui::Render();

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameInfo.commandBuffer);
}
}

#include "GuiManager.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

app::GuiManager::GuiManager()
{
    _beginGuiReceiver = panda::utils::Signals::beginGuiRender.connect([this](auto commandBuffer, auto& scene) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        _devGui.render();
        _userGui.render(scene);

        ImGui::Render();

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer, VK_NULL_HANDLE);
    });
}

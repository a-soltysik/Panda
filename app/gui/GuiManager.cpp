#include "GuiManager.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

app::GuiManager::GuiManager()
{
    _beginGuiReceiver = panda::utils::signals::beginGuiRender.connect([this](auto data) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        _devGui.render();
        _userGui.render(data.scene);

        ImGui::Render();

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), data.commandBuffer, VK_NULL_HANDLE);
    });
}

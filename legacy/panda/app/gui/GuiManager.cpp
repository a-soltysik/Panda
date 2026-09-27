// clang-format off
#include "panda/utils/Assert.h" // NOLINT(misc-include-cleaner)
// clang-format on

#include "GuiManager.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <panda/utils/Signals.h>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>  // NOLINT(misc-include-cleaner)

#include "gui/DevGui.h"
#include "gui/UserGui.h"

namespace panda
{
class Window;
}

app::GuiManager::GuiManager(const panda::Window& window)
    : _devGui {window},
      _userGui {window}
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

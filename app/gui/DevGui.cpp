#include "DevGui.h"

#include <imgui.h>

namespace app
{

auto DevGui::render() -> void
{
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);

    ImGui::Begin("Frame profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                static_cast<double>(1000.0f / frameRate),
                static_cast<double>(frameRate));
    // NOLINTEND(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    time += ImGui::GetIO().DeltaTime;

    static constexpr auto second = 1.f;
    if (time > second)
    {
        time = 0.f;
        frameRate = ImGui::GetIO().Framerate;
        panda::log::Info("Average FPS: {}", frameRate);
    }
    ImGui::End();
}
}
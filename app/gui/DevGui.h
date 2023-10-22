#pragma once

#include <imgui.h>
#include <panda/utils/Signals.h>

namespace app
{

class DevGui
{
public:
    auto render() -> void;

private:
    float time = 0.f;
    float frameRate = ImGui::GetIO().Framerate;
};

}

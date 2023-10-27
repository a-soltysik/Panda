#include "UserGui.h"

#include <imgui.h>

#include "utils/Utils.h"

namespace app
{

auto UserGui::render(panda::gfx::vulkan::Scene& scene) -> void
{
    ImGui::Begin("Scene Info", nullptr);

    const auto names = scene.objects | std::ranges::views::transform(&panda::gfx::vulkan::Object::getName) |
                       std::ranges::views::transform(&std::string::c_str) | utils::to<std::vector<const char*>>();

    ImGui::ListBox("Objects", &_currentObject, names.data(), static_cast<int>(names.size()), 4);

    if (!scene.objects.empty() && scene.objects.size() > static_cast<size_t>(_currentObject))
    {
        auto& object = scene.objects[static_cast<size_t>(_currentObject)];
        _currentTransform = &object.transform;
        ImGui::DragFloat3("translation", reinterpret_cast<float*>(&_currentTransform->translation), 0.05f);
        ImGui::DragFloat3("scale", reinterpret_cast<float*>(&_currentTransform->scale), 0.05f);
        ImGui::DragFloat3("rotation", reinterpret_cast<float*>(&_currentTransform->rotation), 0.05f);
    }

    ImGui::End();
}
}
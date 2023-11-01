#include "UserGui.h"

#include <imgui.h>

#include "utils/Utils.h"

namespace app
{

UserGui::UserGui(const panda::Window& window)
    : _window {window}
{
}

auto UserGui::render(panda::gfx::vulkan::Scene& scene) -> void
{
    const auto windowSize =
        ImVec2 {static_cast<float>(_window.getSize().x) / 3.f, static_cast<float>(_window.getSize().y)};
    ImGui::SetNextWindowPos({static_cast<float>(_window.getSize().x) * 2.f / 3.f, 0}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    ImGui::Begin("Scene Info", nullptr);

    const auto result = objectListBox(getAllNames(scene));
    objectInfo(scene, result);

    ImGui::End();
}

auto UserGui::baseLight(panda::gfx::BaseLight& light) -> void
{
    ImGui::DragFloat("intensity", &light.intensity, 0.05f, 0.f, 5.f);
    ImGui::DragFloat3("ambient", reinterpret_cast<float*>(&light.ambient), 0.05f, 0.f, 1.f);
    ImGui::DragFloat3("diffuse", reinterpret_cast<float*>(&light.diffuse), 0.05f, 0.f, 1.f);
    ImGui::DragFloat3("specular", reinterpret_cast<float*>(&light.specular), 0.05f, 0.f, 1.f);
}

auto UserGui::directionalLight(panda::gfx::DirectionalLight& light) -> void
{
    baseLight(light);
    ImGui::DragFloat3("direction", reinterpret_cast<float*>(&light.direction), 0.05f);
}

auto UserGui::pointLight(panda::gfx::PointLight& light) -> void
{
    baseLight(light);
    ImGui::DragFloat3("position", reinterpret_cast<float*>(&light.position), 0.05f);
    attenuation(light.attenuation);
}

auto UserGui::attenuation(panda::gfx::Attenuation& attenuation) -> void
{
    ImGui::DragFloat("constant", &attenuation.constant, 0.005f, 0.f, 1.f);
    ImGui::DragFloat("linear", &attenuation.linear, 0.005f, 0.f, 1.f);
    ImGui::DragFloat("exp", &attenuation.exp, 0.005f, 0.f, 1.f);
}

auto UserGui::spotLight(panda::gfx::SpotLight& light) -> void
{
    pointLight(light);
    ImGui::DragFloat3("direction", reinterpret_cast<float*>(&light.direction), 0.05f);
    ImGui::DragFloat("cutoff", &light.cutOff, 0.005f, 0.f, 1.f);
}

auto UserGui::vulkanObject(panda::gfx::vulkan::Object& object) -> void
{
    auto& currentTransform = object.transform;
    ImGui::DragFloat3("translation", reinterpret_cast<float*>(&currentTransform.translation), 0.05f);
    ImGui::DragFloat3("scale", reinterpret_cast<float*>(&currentTransform.scale), 0.05f);
    ImGui::DragFloat3("rotation", reinterpret_cast<float*>(&currentTransform.rotation), 0.05f);
}

auto UserGui::objectListBox(const std::vector<std::string>& objects) -> std::string
{
    const auto names = objects                                               //
                       | std::ranges::views::transform(&std::string::c_str)  //
                       | utils::to<std::vector<const char*>>();

    ImGui::ListBox("Objects",
                   &_currentObject,
                   names.data(),
                   static_cast<int>(names.size()),
                   static_cast<int>(objects.size()));

    return objects[static_cast<size_t>(_currentObject)];
}

auto UserGui::objectInfo(panda::gfx::vulkan::Scene& scene, const std::string& name) -> void
{
    const auto objectIt = std::ranges::find(scene.objects, name, &panda::gfx::vulkan::Object::getName);
    if (objectIt != std::ranges::end(scene.objects))
    {
        vulkanObject(*objectIt);
        return;
    }
    const auto directionalLightIt =
        std::ranges::find(scene.lights.directionalLights, name, &panda::gfx::BaseLight::name);
    if (directionalLightIt != std::ranges::end(scene.lights.directionalLights))
    {
        directionalLight(*directionalLightIt);
        return;
    }
    const auto pointLightIt = std::ranges::find(scene.lights.pointLights, name, &panda::gfx::BaseLight::name);
    if (pointLightIt != std::ranges::end(scene.lights.pointLights))
    {
        pointLight(*pointLightIt);
        return;
    }
    const auto spotLightIt = std::ranges::find(scene.lights.spotLights, name, &panda::gfx::BaseLight::name);
    if (spotLightIt != std::ranges::end(scene.lights.spotLights))
    {
        spotLight(*spotLightIt);
    }
}

auto UserGui::getAllNames(const panda::gfx::vulkan::Scene& scene) -> std::vector<std::string>
{
    const auto objectNames = scene.objects | std::ranges::views::transform(&panda::gfx::vulkan::Object::getName);
    const auto directionalLightNames =
        scene.lights.directionalLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);
    const auto pointLightNames = scene.lights.pointLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);
    const auto spotLightNames = scene.lights.spotLights | std::ranges::views::transform(&panda::gfx::BaseLight::name);

    const auto size =
        objectNames.size() + directionalLightNames.size() + pointLightNames.size() + spotLightNames.size();
    auto result = std::vector<std::string> {};
    result.reserve(size);

    std::ranges::copy(objectNames, std::back_inserter(result));
    std::ranges::copy(directionalLightNames, std::back_inserter(result));
    std::ranges::copy(pointLightNames, std::back_inserter(result));
    std::ranges::copy(spotLightNames, std::back_inserter(result));

    return result;
}

}
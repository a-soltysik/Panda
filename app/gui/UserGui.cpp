#include "UserGui.h"

#include <imgui.h>
#include <portable-file-dialogs.h>

#include "utils/Signals.h"
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
        ImVec2 {static_cast<float>(_window.getSize().x) / 3, static_cast<float>(_window.getSize().y)};
    ImGui::SetNextWindowPos({static_cast<float>(_window.getSize().x) * 2.F / 3.F, 0}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    ImGui::Begin("Scene Info", nullptr);

    const auto objectNames = utils::getNamesFromScene(scene);

    if (!objectNames.empty())
    {
        objectInfo(scene, objectListBox(objectNames));
    }

    if (ImGui::Button("Add model from file"))
    {
        auto file = pfd::open_file("Choose file to save", ".", {"3D Model File", "*"});

        if (!file.result().empty())
        {
            panda::log::Info("Selected file: {}", file.result().front());
            utils::signals::newMeshAdded.registerSender()(
                utils::signals::NewMeshAddedData {_window.getId(), file.result().front()});
        }
    }

    addLight(scene, objectNames);

    if (ImGui::Button("Remove selected object"))
    {
        if (!objectNames.empty())
        {
            removeObject(scene, objectNames);
        }
    }

    ImGui::End();
}

auto UserGui::baseLight(panda::gfx::BaseLight& light) -> void
{
    ImGui::DragFloat("intensity", &light.intensity, 0.05F, 0.F, 5.F);
    ImGui::DragFloat3("ambient", reinterpret_cast<float*>(&light.ambient), 0.05F, 0.F, 1.F);
    ImGui::DragFloat3("diffuse", reinterpret_cast<float*>(&light.diffuse), 0.05F, 0.F, 1.F);
    ImGui::DragFloat3("specular", reinterpret_cast<float*>(&light.specular), 0.05F, 0.F, 1.F);
}

auto UserGui::directionalLight(panda::gfx::DirectionalLight& light) -> void
{
    baseLight(light);
    ImGui::DragFloat3("direction", reinterpret_cast<float*>(&light.direction), 0.05F);
}

auto UserGui::pointLight(panda::gfx::PointLight& light) -> void
{
    baseLight(light);
    ImGui::DragFloat3("position", reinterpret_cast<float*>(&light.position), 0.05F);
    attenuation(light.attenuation);
}

auto UserGui::attenuation(panda::gfx::Attenuation& attenuation) -> void
{
    ImGui::DragFloat("constant", &attenuation.constant, 0.005F, 0.F, 1.F);
    ImGui::DragFloat("linear", &attenuation.linear, 0.005F, 0.F, 1.F);
    ImGui::DragFloat("exp", &attenuation.exp, 0.005F, 0.F, 1.F);
}

auto UserGui::spotLight(panda::gfx::SpotLight& light) -> void
{
    pointLight(light);
    ImGui::DragFloat3("direction", reinterpret_cast<float*>(&light.direction), 0.05F);
    ImGui::DragFloat("cutoff", &light.cutOff, 0.005F, 0.F, 1.F);
}

auto UserGui::vulkanObject(panda::gfx::vulkan::Object& object) -> void
{
    auto& currentTransform = object.transform;
    ImGui::DragFloat3("translation", reinterpret_cast<float*>(&currentTransform.translation), 0.05F);
    ImGui::DragFloat3("scale", reinterpret_cast<float*>(&currentTransform.scale), 0.05F);
    ImGui::DragFloat3("rotation", reinterpret_cast<float*>(&currentTransform.rotation), 0.05F);
}

auto UserGui::objectListBox(const std::vector<std::string>& objects) -> std::string
{
    const auto names = objects                                               //
                       | std::ranges::views::transform(&std::string::c_str)  //
                       | utils::to<std::vector<const char*>>();

    if (objects.empty())
    {
        return {};
    }

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

auto UserGui::removeObject(panda::gfx::vulkan::Scene& scene, const std::vector<std::string>& objects) -> void
{
    const auto& currentName = objects[static_cast<size_t>(_currentObject)];
    _currentObject = std::max(_currentObject - 1, 0);

    const auto objectIt = std::ranges::find(scene.objects, currentName, &panda::gfx::vulkan::Object::getName);
    if (objectIt != std::ranges::end(scene.objects))
    {
        scene.objects.erase(objectIt);
        return;
    }
    const auto directionalLightIt =
        std::ranges::find(scene.lights.directionalLights, currentName, &panda::gfx::BaseLight::name);
    if (directionalLightIt != std::ranges::end(scene.lights.directionalLights))
    {
        scene.lights.directionalLights.erase(directionalLightIt);
        return;
    }
    const auto pointLightIt = std::ranges::find(scene.lights.pointLights, currentName, &panda::gfx::BaseLight::name);
    if (pointLightIt != std::ranges::end(scene.lights.pointLights))
    {
        scene.lights.pointLights.erase(pointLightIt);
        return;
    }
    const auto spotLightIt = std::ranges::find(scene.lights.spotLights, currentName, &panda::gfx::BaseLight::name);
    if (spotLightIt != std::ranges::end(scene.lights.spotLights))
    {
        scene.lights.spotLights.erase(spotLightIt);
    }
}

auto UserGui::addLight(panda::gfx::vulkan::Scene& scene, const std::vector<std::string>& objects) -> void
{
    static constexpr auto lightsNames = std::array {"Directional", "Point", "Spot"};
    ImGui::Combo("Light", &_currentLight, lightsNames.data(), lightsNames.size());

    ImGui::SameLine();

    if (ImGui::Button("Add light"))
    {
        switch (_currentLight)
        {
        case 0:
            if (scene.lights.directionalLights.size() >= panda::gfx::maxLights)
            {
                panda::log::Warning("Can't add more directional lights. Max number is {}", panda::gfx::maxLights);
            }
            else
            {
                scene.lights.directionalLights.push_back(panda::gfx::DirectionalLight {
                    panda::gfx::makeColorLight(utils::getUniqueName("DirectionalLight", objects),
                                               {1, 1,  1},
                                               0.F,
                                               0.8F,
                                               1.F,
                                               0.1F),
                    {0, -1, 0},
                });
            }
            break;
        case 1:
            if (scene.lights.pointLights.size() >= panda::gfx::maxLights)
            {
                panda::log::Warning("Can't add more point lights. Max number is {}", panda::gfx::maxLights);
            }
            else
            {
                scene.lights.pointLights.push_back(panda::gfx::PointLight {
                    panda::gfx::makeColorLight(utils::getUniqueName("PointLight", objects),
                                               {1,   1,     1     },
                                               0.F,
                                               0.8F,
                                               1.F,
                                               0.1F),
                    {0,   0,     0     },
                    {1.F, 0.05F, 0.005F}
                });
            }
            break;
        case 2:
            if (scene.lights.spotLights.size() >= panda::gfx::maxLights)
            {
                panda::log::Warning("Can't add more spot lights. Max number is {}", panda::gfx::maxLights);
            }
            else
            {
                scene.lights.spotLights.push_back(panda::gfx::SpotLight {
                    {panda::gfx::makeColorLight(utils::getUniqueName("SpotLight", objects),
                     {1, 1, 1},
                     0.F, 0.8F,
                     1.F, 0.1F),
                     {0.F, -5.F, 0.F},
                     {1.F, 0.05F, 0.005F}},
                    {0.F, 1.F, 0.F},
                    glm::cos(glm::radians(30.F))
                });
            }
            break;
        }
    }
}
}
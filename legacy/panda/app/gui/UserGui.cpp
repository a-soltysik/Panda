#include "UserGui.h"

#include <imgui.h>
#include <panda/Logger.h>
#include <panda/Window.h>
#include <panda/gfx/Light.h>
#include <panda/gfx/vulkan/Scene.h>
#include <panda/gfx/vulkan/object/Object.h>
#include <panda/utils/Signal.h>
#include <panda/utils/Utils.h>
#include <portable-file-dialogs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <glm/trigonometric.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

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

    const auto& objectNames = scene.getAllNames();

    if (!objectNames.empty())
    {
        _currentObject = objectListBox(objectNames);
        objectInfo(scene, _currentObject);
    }

    if (ImGui::Button("Add model from file"))
    {
        auto file = pfd::open_file("Choose file to open", ".", {"3D Model File", "*"});

        if (!file.result().empty())
        {
            panda::log::Info("Selected file: {}", file.result().front());
            utils::signals::newMeshAdded.registerSender()(
                utils::signals::NewMeshAddedData {.id = _window.getId(), .fileName = file.result().front()});
        }
    }

    addLight(scene);

    if (ImGui::Button("Remove selected object"))
    {
        removeObject(scene);
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

auto UserGui::objectListBox(const std::unordered_set<std::string_view>& objects) -> std::string
{
    const auto names =
        objects | std::ranges::views::transform(&std::string_view::data) | utils::to<std::vector<const char*>>();

    _currentIndex = std::min(_currentIndex, static_cast<int>(names.size() - 1));

    ImGui::ListBox("Objects",
                   &_currentIndex,
                   names.data(),
                   static_cast<int>(names.size()),
                   static_cast<int>(names.size()));

    return names[static_cast<size_t>(_currentIndex)];
}

auto UserGui::objectInfo(panda::gfx::vulkan::Scene& scene, const std::string& name) -> void
{
    if (const auto object = scene.findObjectByName(name); object.has_value())
    {
        vulkanObject(*object.value());
        return;
    }
    std::visit(panda::utils::overload {[](std::reference_wrapper<panda::gfx::DirectionalLight> light) {
                                           directionalLight(light.get());
                                       },
                                       [](std::reference_wrapper<panda::gfx::PointLight> light) {
                                           pointLight(light.get());
                                       },
                                       [](std::reference_wrapper<panda::gfx::SpotLight> light) {
                                           spotLight(light.get());
                                       },
                                       [](auto) {

                                       }},
               scene.findLightByName(name));
}

auto UserGui::removeObject(panda::gfx::vulkan::Scene& scene) -> void
{
    if (scene.removeObjectByName(_currentObject))
    {
        return;
    }
    scene.removeLightByName(_currentObject);
}

auto UserGui::addLight(panda::gfx::vulkan::Scene& scene) -> void
{
    static constexpr auto lightsNames = std::array {"Directional", "Point", "Spot"};
    ImGui::Combo("Light", &_currentLight, lightsNames.data(), lightsNames.size());

    ImGui::SameLine();

    if (ImGui::Button("Add light"))
    {
        switch (_currentLight)
        {
        case 0:
            if (const auto light = scene.addLight<panda::gfx::DirectionalLight>("DirectionalLight"); light.has_value())
            {
                light->get().makeColorLight({1, 1, 1}, 0.F, 0.8F, 1.F, 0.1F);
                light->get().direction = {0, -1, 0};
            }
            else
            {
                panda::log::Warning("Can't add more directional lights. Max number is {}", panda::gfx::maxLights);
            }
            break;
        case 1:
            if (const auto light = scene.addLight<panda::gfx::PointLight>("PointLight"); light.has_value())
            {
                light->get().makeColorLight({1, 1, 1}, 0.F, 0.8F, 1.F, 0.1F);
                light->get().position = {0, 0, 0};
                light->get().attenuation = {.constant = 1.F, .linear = 0.05F, .exp = 0.005F};
            }
            else
            {
                panda::log::Warning("Can't add more point lights. Max number is {}", panda::gfx::maxLights);
            }
            break;
        case 2:
            if (const auto light = scene.addLight<panda::gfx::SpotLight>("SpotLight"); light.has_value())
            {
                light->get().makeColorLight({1, 1, 1}, 0.F, 0.8F, 1.F, 0.1F);
                light->get().position = {0.F, -5.F, 0.F},
                light->get().attenuation = {.constant = 1.F, .linear = 0.05F, .exp = 0.005F};
                light->get().direction = {0.F, 1.F, 0.F};
                light->get().cutOff = glm::cos(glm::radians(30.F));
            }
            else
            {
                panda::log::Warning("Can't add more spot lights. Max number is {}", panda::gfx::maxLights);
            }
            break;
        default:
            break;
        }
    }
}
}

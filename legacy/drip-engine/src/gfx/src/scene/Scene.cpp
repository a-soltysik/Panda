#include "drip/gfx/scene/Scene.hpp"

#include <algorithm>
#include <drip/common/log/LogMessageBuilder.hpp>
#include <drip/common/utils/Utils.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "drip/gfx/resource/MeshRenderable.hpp"
#include "drip/gfx/resource/Renderable.hpp"
#include "drip/gfx/scene/Camera.hpp"
#include "drip/gfx/scene/Light.hpp"

namespace drip::gfx
{

auto Scene::getLights() const noexcept -> const Lights&
{
    return _lights;
}

auto Scene::getLights() noexcept -> Lights&
{
    return _lights;
}

auto Scene::getCamera() const noexcept -> const Camera&
{
    return _camera;
}

auto Scene::getCamera() noexcept -> Camera&
{
    return _camera;
}

auto Scene::getRenderables() const noexcept -> const std::vector<std::unique_ptr<Renderable>>&
{
    return _renderables;
}

auto Scene::addRenderable(std::unique_ptr<Renderable> renderable) -> bool
{
    const auto name = renderable->getName();
    if (isNameUsed(name))
    {
        common::log::Warning("Name {} is already used", name);
        return false;
    }
    _renderableNames.emplace(name, renderable.get());
    _renderables.push_back(std::move(renderable));
    common::log::Info("Added renderable with name {}", name);
    return true;
}

auto Scene::removeRenderableByName(std::string_view name) -> bool
{
    const auto nameIt = _renderableNames.find(name);
    if (nameIt == _renderableNames.end())
    {
        common::log::Warning("Name {} is not found", name);
        return false;
    }

    auto* renderablePtr = nameIt->second;
    _renderableNames.erase(nameIt);

    const auto renderableIt = std::ranges::find(_renderables, renderablePtr, &std::unique_ptr<Renderable>::get);
    if (renderableIt == std::ranges::end(_renderables))
    {
        common::log::Warning("Renderable with name {} is not found", name);
        return false;
    }
    _renderables.erase(renderableIt);
    common::log::Info("Removed renderable with name {}", name);
    return true;
}

auto Scene::findRenderableByName(std::string_view name) -> std::optional<std::reference_wrapper<Renderable>>
{
    const auto nameIt = _renderableNames.find(name);
    if (nameIt == _renderableNames.end())
    {
        common::log::Warning("Name {} is not found", name);
        return {};
    }
    return *nameIt->second;
}

auto Scene::findRenderableByName(std::string_view name) const -> std::optional<std::reference_wrapper<const Renderable>>
{
    const auto nameIt = _renderableNames.find(name);
    if (nameIt == _renderableNames.end())
    {
        common::log::Warning("Name {} is not found", name);
        return {};
    }
    return *nameIt->second;
}

auto Scene::addLight(std::variant<DirectionalLight, PointLight, SpotLight> lightVariant) -> bool
{
    return std::visit(
        common::utils::overload {[this](const DirectionalLight& light) {
                                     if (isNameUsed(light.name))
                                     {
                                         common::log::Warning("Name {} is already used", light.name);
                                         return false;
                                     }
                                     _lights.directionalLights.push_back(light);
                                     _lightNames.emplace(light.name, std::ref(_lights.directionalLights.back()));
                                     return true;
                                 },
                                 [this](const PointLight& light) {
                                     if (isNameUsed(light.name))
                                     {
                                         common::log::Warning("Name {} is already used", light.name);
                                         return false;
                                     }
                                     _lights.pointLights.push_back(light);
                                     _lightNames.emplace(light.name, std::ref(_lights.pointLights.back()));
                                     return true;
                                 },
                                 [this](const SpotLight& light) {
                                     if (isNameUsed(light.name))
                                     {
                                         common::log::Warning("Name {} is already used", light.name);
                                         return false;
                                     }
                                     _lights.spotLights.push_back(light);
                                     _lightNames.emplace(light.name, std::ref(_lights.spotLights.back()));
                                     return true;
                                 }},
        lightVariant);
}

auto Scene::findLightByName(std::string_view name) -> std::variant<std::reference_wrapper<DirectionalLight>,
                                                                   std::reference_wrapper<PointLight>,
                                                                   std::reference_wrapper<SpotLight>,
                                                                   std::monostate>
{
    const auto nameIt = _lightNames.find(name);
    if (nameIt == _lightNames.end())
    {
        common::log::Warning("Name {} is not found", name);
        return std::monostate {};
    }
    return std::visit<std::variant<std::reference_wrapper<DirectionalLight>,
                                   std::reference_wrapper<PointLight>,
                                   std::reference_wrapper<SpotLight>,
                                   std::monostate>>(common::utils::overload {[](DirectionalLight& light) {
                                                                                 return std::ref(light);
                                                                             },
                                                                             [](PointLight& light) {
                                                                                 return std::ref(light);
                                                                             },
                                                                             [](SpotLight& light) {
                                                                                 return std::ref(light);
                                                                             }},
                                                    nameIt->second);
}

auto Scene::isNameUsed(std::string_view name) const -> bool
{
    return _lightNames.contains(name) || _renderableNames.contains(name);
}

}

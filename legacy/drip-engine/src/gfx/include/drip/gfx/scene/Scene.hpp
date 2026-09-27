#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "drip/gfx/resource/MeshRenderable.hpp"
#include "drip/gfx/resource/Renderable.hpp"
#include "drip/gfx/scene/Camera.hpp"
#include "drip/gfx/scene/Light.hpp"

namespace drip::gfx
{

class Scene
{
public:
    [[nodiscard]] auto getLights() const noexcept -> const Lights&;
    [[nodiscard]] auto getLights() noexcept -> Lights&;
    [[nodiscard]] auto getCamera() const noexcept -> const Camera&;
    [[nodiscard]] auto getCamera() noexcept -> Camera&;
    [[nodiscard]] auto getRenderables() const noexcept -> const std::vector<std::unique_ptr<Renderable>>&;

    auto addRenderable(std::unique_ptr<Renderable> renderable) -> bool;
    auto removeRenderableByName(std::string_view name) -> bool;
    auto findRenderableByName(std::string_view name) -> std::optional<std::reference_wrapper<Renderable>>;
    auto findRenderableByName(std::string_view name) const -> std::optional<std::reference_wrapper<const Renderable>>;

    [[nodiscard]] auto findLightByName(std::string_view name) -> std::variant<std::reference_wrapper<DirectionalLight>,
                                                                              std::reference_wrapper<PointLight>,
                                                                              std::reference_wrapper<SpotLight>,
                                                                              std::monostate>;

    auto addLight(std::variant<DirectionalLight, PointLight, SpotLight> lightVariant) -> bool;

private:
    auto isNameUsed(std::string_view name) const -> bool;

    std::vector<std::unique_ptr<Renderable>> _renderables;
    std::unordered_map<std::string_view, Renderable*> _renderableNames;
    std::unordered_map<std::string_view,
                       std::variant<std::reference_wrapper<DirectionalLight>,
                                    std::reference_wrapper<PointLight>,
                                    std::reference_wrapper<SpotLight>>>
        _lightNames;
    Lights _lights;
    Camera _camera;
};

}

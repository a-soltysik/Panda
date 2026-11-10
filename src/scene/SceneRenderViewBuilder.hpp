#pragma once

#include <cstddef>

#include "SceneState.hpp"

namespace panda::scene::detail
{
class SceneRenderViewBuilder final
{
public:
    explicit SceneRenderViewBuilder(SceneState& scene);

    [[nodiscard]] auto build(float aspectRatio) -> Result<RenderView>;

private:
    [[nodiscard]] static auto makeCameraFromProjection(const glm::mat4& world, const glm::mat4& projection)
        -> Result<RenderCamera>;
    [[nodiscard]] static auto makeCamera(const glm::mat4& world, const PerspectiveCamera& lens, float aspectRatio)
        -> Result<RenderCamera>;
    [[nodiscard]] static auto makeCamera(const glm::mat4& world, const OrthographicCamera& lens, float aspectRatio)
        -> Result<RenderCamera>;
    [[nodiscard]] auto activeRenderCamera(float aspectRatio) -> Result<RenderCamera>;
    [[nodiscard]] auto appendRenderObject(std::size_t index, RenderView& view, bool& hasLight) -> Result<void>;
    [[nodiscard]] auto appendLight(std::size_t index, RenderView& view, bool& hasLight) -> Result<void>;
    [[nodiscard]] auto collectRenderObjects(RenderView& view) -> Result<void>;

    SceneState& _scene;
};
}

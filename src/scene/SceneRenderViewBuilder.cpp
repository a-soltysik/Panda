#include "SceneRenderViewBuilder.hpp"

#include <cstddef>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>
#include <panda/scene/Scene.hpp>

#include "Math.hpp"
#include "SceneState.hpp"
#include "SceneTransformMath.hpp"

namespace panda::scene::detail
{
using ::panda::detail::isFinite;

SceneRenderViewBuilder::SceneRenderViewBuilder(SceneState& scene)
    : _scene {scene}
{
}

auto SceneRenderViewBuilder::makeCameraFromProjection(const glm::mat4& world, const glm::mat4& projection)
    -> Result<RenderCamera>
{
    auto rotation = extractPoseRotation(world);
    if (!rotation)
    {
        return std::unexpected {rotation.error()};
    }
    const auto position = glm::vec3 {world[3]};
    const auto view = glm::mat4_cast(glm::conjugate(*rotation)) * glm::translate(glm::mat4 {1.0F}, -position);
    const auto viewProjection = projection * view;
    if (!isFinite(viewProjection))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Camera matrix is not representable")};
    }
    return RenderCamera {.viewProjection = viewProjection, .position = position};
}

auto SceneRenderViewBuilder::makeCamera(const glm::mat4& world, const PerspectiveCamera& lens, float aspectRatio)
    -> Result<RenderCamera>
{
    auto projection =
        createPerspectiveProjection(lens.verticalFieldOfViewRadians, aspectRatio, lens.nearPlane, lens.farPlane);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    return makeCameraFromProjection(world, *projection);
}

auto SceneRenderViewBuilder::makeCamera(const glm::mat4& world, const OrthographicCamera& lens, float aspectRatio)
    -> Result<RenderCamera>
{
    auto projection = createOrthographicProjection(lens.height, aspectRatio, lens.nearPlane, lens.farPlane);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    return makeCameraFromProjection(world, *projection);
}

auto SceneRenderViewBuilder::activeRenderCamera(float aspectRatio) -> Result<RenderCamera>
{
    const auto cameraIndex = _scene._activeCamera;
    if (!cameraIndex)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Scene has no active camera")};
    }
    const auto& slot = _scene._slots[*cameraIndex];
    if (!slot.camera && !slot.orthographicCamera)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Active object has no camera lens")};
    }
    auto updated = _scene.updateWorld(*cameraIndex);
    if (!updated)
    {
        return std::unexpected {updated.error()};
    }
    if (slot.camera)
    {
        return makeCamera(slot.worldMatrix, *slot.camera, aspectRatio);
    }
    return makeCamera(slot.worldMatrix, *slot.orthographicCamera, aspectRatio);
}

auto SceneRenderViewBuilder::appendRenderObject(std::size_t index, RenderView& view) -> Result<void>
{
    const auto& slot = _scene._slots[index];
    if (!slot.alive || !slot.visible || (!slot.mesh && !slot.light))
    {
        return {};
    }
    auto updated = _scene.updateWorld(index);
    if (!updated)
    {
        return updated;
    }
    if (slot.mesh)
    {
        view.meshes.push_back({.mesh = slot.mesh->mesh, .modelMatrix = slot.worldMatrix, .tint = slot.mesh->tint});
    }
    return appendLight(index, view);
}

auto SceneRenderViewBuilder::appendLight(std::size_t index, RenderView& view) -> Result<void>
{
    const auto& slot = _scene._slots[index];
    if (slot.light)
    {
        auto rotation = extractPoseRotation(slot.worldMatrix);
        if (!rotation)
        {
            return std::unexpected {rotation.error()};
        }
        const auto light = panda::DirectionalLight {
            .direction = *rotation * glm::vec3 {0.0F, 0.0F, -1.0F},
            .color = slot.light->color,
            .intensity = slot.light->intensity
        };
        view.directionalLights.push_back(light);
    }
    return {};
}

auto SceneRenderViewBuilder::collectRenderObjects(RenderView& view) -> Result<void>
{
    for (auto index = std::size_t {0}; index < _scene._slots.size(); ++index)
    {
        auto result = appendRenderObject(index, view);
        if (!result)
        {
            return std::unexpected {result.error()};
        }
    }
    return {};
}

auto SceneRenderViewBuilder::build(float aspectRatio) -> Result<RenderView>
{
    auto camera = activeRenderCamera(aspectRatio);
    if (!camera)
    {
        return std::unexpected {camera.error()};
    }
    auto view = RenderView {};
    view.camera = *camera;
    auto collected = collectRenderObjects(view);
    if (!collected)
    {
        return std::unexpected {collected.error()};
    }
    return view;
}
}

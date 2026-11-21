#include <glm/ext/matrix_float4x4.hpp>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/math/Transform.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/ObjectId.hpp>
#include <panda/scene/Scene.hpp>
#include <string>
#include <utility>

#include "SceneState.hpp"

namespace panda::scene
{
Scene::Scene()
    : _state {std::make_unique<detail::SceneState>()}
{
}

Scene::Scene(Scene&& other) noexcept = default;

auto Scene::operator=(Scene&& other) noexcept -> Scene& = default;

Scene::~Scene() = default;

auto Scene::createEntity(const EntityDesc& description) -> Result<Entity>
{
    return expect(_state, "Cannot use a moved-from Scene").createEntity(description);
}

auto Scene::createEntity(ObjectId objectId, const EntityDesc& description) -> Result<Entity>
{
    return expect(_state, "Cannot use a moved-from Scene").createEntity(objectId, description);
}

auto Scene::getObjectId(const Entity& entity) const -> Result<ObjectId>
{
    return expect(_state, "Cannot use a moved-from Scene").getObjectId(entity);
}

auto Scene::findEntity(ObjectId objectId) const -> std::optional<Entity>
{
    return expect(_state, "Cannot use a moved-from Scene").findEntity(objectId);
}

auto Scene::isAlive(const Entity& entity) const noexcept -> bool
{
    return expect(_state, "Cannot use a moved-from Scene").isAlive(entity);
}

auto Scene::getLocalTransform(const Entity& entity) const -> Result<Transform>
{
    return expect(_state, "Cannot use a moved-from Scene").getLocalTransform(entity);
}

auto Scene::getName(const Entity& entity) const -> Result<std::string>
{
    return expect(_state, "Cannot use a moved-from Scene").getName(entity);
}

auto Scene::setName(const Entity& entity, std::string name) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setName(entity, std::move(name));
}

auto Scene::setLocalTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setLocalTransform(entity, transform);
}

auto Scene::getWorldTransform(const Entity& entity) -> Result<glm::mat4>
{
    return expect(_state, "Cannot use a moved-from Scene").getWorldTransform(entity);
}

auto Scene::setWorldTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setWorldTransform(entity, transform);
}

auto Scene::getParent(const Entity& entity) const -> Result<std::optional<Entity>>
{
    return expect(_state, "Cannot use a moved-from Scene").getParent(entity);
}

auto Scene::setParent(const Entity& entity, const std::optional<Entity>& parent, ReparentMode mode) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setParent(entity, parent, mode);
}

auto Scene::destroyEntity(const Entity& entity) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").destroyEntity(entity);
}

auto Scene::destroySubtree(const Entity& entity) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").destroySubtree(entity);
}

auto Scene::setMesh(const Entity& entity, std::optional<MeshInstance> mesh) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setMesh(entity, std::move(mesh));
}

auto Scene::setCamera(const Entity& entity, std::optional<PerspectiveCamera> camera) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setCamera(entity, camera);
}

auto Scene::setOrthographicCamera(const Entity& entity, std::optional<OrthographicCamera> camera) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setOrthographicCamera(entity, camera);
}

auto Scene::setLight(const Entity& entity, std::optional<Light> light) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setLight(entity, light);
}

void Scene::setEnvironment(std::optional<EnvironmentLighting> environment)
{
    expect(_state, "Cannot use a moved-from Scene").setEnvironment(std::move(environment));
}

void Scene::setSkybox(std::optional<Texture> skybox)
{
    expect(_state, "Cannot use a moved-from Scene").setSkybox(std::move(skybox));
}

auto Scene::setVisible(const Entity& entity, bool visible) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setVisible(entity, visible);
}

auto Scene::setActiveCamera(const Entity& entity) -> Result<void>
{
    return expect(_state, "Cannot use a moved-from Scene").setActiveCamera(entity);
}

auto Scene::createRenderView(float aspectRatio) -> Result<RenderView>
{
    return expect(_state, "Cannot use a moved-from Scene").createRenderView(aspectRatio);
}
}

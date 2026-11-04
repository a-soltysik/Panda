#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <memory>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/scene/Scene.hpp>
#include <string>
#include <utility>

#include "SceneState.hpp"

namespace panda::scene
{
auto Entity::operator==(const Entity& other) const noexcept -> bool
{
    return _slot == other._slot && _slotReuseCount == other._slotReuseCount && !_scene.owner_before(other._scene) &&
           !other._scene.owner_before(_scene);
}

Scene::Scene(Context& context, std::unique_ptr<detail::SceneState> state) noexcept
    : _context {&context},
      _state {std::move(state)}
{
}

auto Scene::create(Context& context) -> Result<Scene>
{
    if (!context.isValid())
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Cannot create a Scene from a moved-from Context")};
    }
    return Scene {context, std::make_unique<detail::SceneState>()};
}

Scene::Scene(Scene&& other) noexcept
    : _context {std::exchange(other._context, nullptr)},
      _state {std::move(other._state)}
{
}

auto Scene::operator=(Scene&& other) noexcept -> Scene&
{
    if (this != &other)
    {
        _context = std::exchange(other._context, nullptr);
        _state = std::move(other._state);
    }
    return *this;
}

Scene::~Scene() = default;

auto Scene::createEntity(const EntityDesc& description) -> Result<Entity>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->createEntity(description);
}

auto Scene::isAlive(const Entity& entity) const noexcept -> bool
{
    return _state && _state->isAlive(entity);
}

auto Scene::getLocalTransform(const Entity& entity) const -> Result<Transform>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->getLocalTransform(entity);
}

auto Scene::getName(const Entity& entity) const -> Result<std::string>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->getName(entity);
}

auto Scene::setName(const Entity& entity, std::string name) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setName(entity, std::move(name));
}

auto Scene::setLocalTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setLocalTransform(entity, transform);
}

auto Scene::getWorldTransform(const Entity& entity) -> Result<glm::mat4>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->getWorldTransform(entity);
}

auto Scene::setWorldTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setWorldTransform(entity, transform);
}

auto Scene::getParent(const Entity& entity) const -> Result<std::optional<Entity>>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->getParent(entity);
}

auto Scene::setParent(const Entity& entity, const std::optional<Entity>& parent, ReparentMode mode) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setParent(entity, parent, mode);
}

auto Scene::destroyEntity(const Entity& entity) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->destroyEntity(entity);
}

auto Scene::destroySubtree(const Entity& entity) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->destroySubtree(entity);
}

auto Scene::setMesh(const Entity& entity, std::optional<MeshInstance> mesh) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setMesh(entity, std::move(mesh));
}

auto Scene::setCamera(const Entity& entity, std::optional<PerspectiveCamera> camera) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setCamera(entity, camera);
}

auto Scene::setDirectionalLight(const Entity& entity, std::optional<DirectionalLight> light) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setDirectionalLight(entity, light);
}

auto Scene::setVisible(const Entity& entity, bool visible) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setVisible(entity, visible);
}

auto Scene::setActiveCamera(const Entity& entity) -> Result<void>
{
    if (!_state)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot use a moved-from Scene")};
    }
    return _state->setActiveCamera(entity);
}

auto Scene::createRenderView(float aspectRatio) -> Result<RenderView>
{
    if (!_state || _context == nullptr || !_context->isValid())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Scene requires a live Context")};
    }
    return _state->createRenderView(aspectRatio);
}
}

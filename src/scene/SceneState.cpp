#include "SceneState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vector_relational.hpp>
#include <iterator>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/ObjectId.hpp>
#include <panda/scene/Scene.hpp>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "Math.hpp"
#include "Overloaded.hpp"
#include "SceneRenderViewBuilder.hpp"
#include "SceneTransformMath.hpp"

namespace panda::scene::detail
{
using ::panda::detail::isFinite;

namespace
{
auto transformError() -> Error
{
    return makeError(ErrorCode::InvalidArgument, "Entity handle is stale or belongs to another Scene");
}

auto validateCamera(const std::optional<PerspectiveCamera>& camera) -> Result<void>
{
    if (!camera)
    {
        return {};
    }
    auto projection =
        createPerspectiveProjection(camera->verticalFieldOfViewRadians, 1.0F, camera->nearPlane, camera->farPlane);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    return {};
}

auto validateCamera(const std::optional<OrthographicCamera>& camera) -> Result<void>
{
    if (!camera)
    {
        return {};
    }
    auto projection = createOrthographicProjection(camera->height, 1.0F, camera->nearPlane, camera->farPlane);
    if (!projection)
    {
        return std::unexpected {projection.error()};
    }
    return {};
}

auto isValidLightColorAndIntensity(glm::vec3 color, float intensity) -> bool
{
    return isFinite(color) && std::isfinite(intensity) && intensity >= 0.0F &&
           !glm::any(glm::lessThan(color, glm::vec3 {0.0F}));
}

auto isValidLightRange(float range) -> bool
{
    return std::isfinite(range) && range > 0.0F;
}

auto isValidSpotCone(float inner, float outer) -> bool
{
    return std::isfinite(inner) && std::isfinite(outer) && inner >= 0.0F && outer > inner &&
           outer <= glm::half_pi<float>();
}

auto isValidShadowSettings(const std::optional<ShadowSettings>& shadow) -> bool
{
    if (!shadow)
    {
        return true;
    }
    constexpr auto resolutions = std::array {256U, 512U, 1024U, 2048U};
    return (!shadow->resolution || std::ranges::find(resolutions, *shadow->resolution) != resolutions.end()) &&
           std::isfinite(shadow->depthBias) && shadow->depthBias >= 0.0F && std::isfinite(shadow->normalOffset) &&
           shadow->normalOffset >= 0.0F;
}

auto isValidLightData(const DirectionalLight& light) -> bool
{
    return isValidLightColorAndIntensity(light.color, light.intensity) && isValidShadowSettings(light.shadow) &&
           (!light.shadow || (std::isfinite(light.shadowCoverageMeters) && light.shadowCoverageMeters > 0.0F));
}

auto isValidLightData(const PointLight& light) -> bool
{
    return isValidLightColorAndIntensity(light.color, light.intensity) && isValidLightRange(light.range) &&
           isValidShadowSettings(light.shadow);
}

auto isValidLightData(const SpotLight& light) -> bool
{
    return isValidLightColorAndIntensity(light.color, light.intensity) && isValidLightRange(light.range) &&
           isValidSpotCone(light.innerConeAngleRadians, light.outerConeAngleRadians) &&
           isValidShadowSettings(light.shadow);
}

auto isValidSceneLight(const Light& light) -> bool
{
    return std::visit(::panda::detail::Overloaded {[](const DirectionalLight& value) {
                                                       return isValidLightData(value);
                                                   },
                                                   [](const PointLight& value) {
                                                       return isValidLightData(value);
                                                   },
                                                   [](const SpotLight& value) {
                                                       return isValidLightData(value);
                                                   }},
                      light);
}
}

auto SceneState::createEntity(const EntityDesc& description) -> Result<Entity>
{
    auto validated = validateTransform(description.transform);
    if (!validated)
    {
        return std::unexpected {validated.error()};
    }
    return insertEntity(generateObjectId(), description, *validated);
}

auto SceneState::createEntity(ObjectId objectId, const EntityDesc& description) -> Result<Entity>
{
    auto validated = validateTransform(description.transform);
    if (!validated)
    {
        return std::unexpected {validated.error()};
    }
    return insertEntity(objectId, description, *validated);
}

auto SceneState::insertEntity(ObjectId objectId,
                              const EntityDesc& description,
                              const std::pair<Transform, glm::mat4>& validatedTransform) -> Result<Entity>
{
    if (_objectSlots.contains(objectId))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Scene already used this ObjectId")};
    }
    auto slotIndex = allocateSlot();
    auto& slot = slotAt(slotIndex);
    slot = Slot {.name = description.name,
                 .objectId = objectId,
                 .local = validatedTransform.first,
                 .localMatrix = validatedTransform.second,
                 .worldMatrix = validatedTransform.second,
                 .parent = std::nullopt,
                 .children = {},
                 .reuseCount = slot.reuseCount,
                 .alive = true,
                 .dirty = false,
                 .mesh = std::nullopt,
                 .camera = std::nullopt,
                 .orthographicCamera = std::nullopt,
                 .light = std::nullopt};
    _objectSlots.emplace(objectId, slotIndex);
    return makeEntity(slotIndex);
}

auto SceneState::generateObjectId() -> ObjectId
{
    auto candidate = _nextObjectId;
    while (_objectSlots.contains(candidate))
    {
        ++_nextObjectId._value;
        candidate = _nextObjectId;
    }
    ++_nextObjectId._value;
    return candidate;
}

auto SceneState::makeEntity(std::size_t index) const -> Entity
{
    auto entity = Entity {};
    entity._scene = _identity;
    entity._slot = index;
    entity._slotReuseCount = slotAt(index).reuseCount;
    return entity;
}

auto SceneState::allocateSlot() -> std::size_t
{
    if (!_freeSlots.empty())
    {
        const auto slot = _freeSlots.back();
        _freeSlots.pop_back();
        return slot;
    }
    _slots.emplace_back();
    return _slots.size() - 1;
}

auto SceneState::isAlive(const Entity& entity) const noexcept -> bool
{
    const auto identity = entity._scene.lock();
    return identity && identity.get() == _identity.get() && entity._slot < _slots.size() &&
           slotAt(entity._slot).alive && slotAt(entity._slot).reuseCount == entity._slotReuseCount;
}

auto SceneState::getObjectId(const Entity& entity) const -> Result<ObjectId>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    const auto& objectId = slotAt(*slot).objectId;
    if (!objectId)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Live Scene entity is missing its ObjectId")};
    }
    return *objectId;
}

auto SceneState::findEntity(ObjectId objectId) const -> std::optional<Entity>
{
    const auto found = _objectSlots.find(objectId);
    if (found == _objectSlots.end())
    {
        return std::nullopt;
    }
    if (!found->second.has_value())
    {
        return std::nullopt;
    }
    return makeEntity(found->second.value());
}

auto SceneState::getLocalTransform(const Entity& entity) const -> Result<Transform>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    return slotAt(*slot).local;
}

auto SceneState::getName(const Entity& entity) const -> Result<std::string>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    return slotAt(*slot).name;
}

auto SceneState::setName(const Entity& entity, std::string name) -> Result<void>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    slotAt(*slot).name = std::move(name);
    return {};
}

auto SceneState::setLocalTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    auto slotIndex = resolve(entity);
    if (!slotIndex)
    {
        return std::unexpected {slotIndex.error()};
    }
    auto validated = validateTransform(transform);
    if (!validated)
    {
        return std::unexpected {validated.error()};
    }
    auto& slot = slotAt(*slotIndex);
    auto parentWorld = getParentWorld(slot.parent);
    if (!parentWorld)
    {
        return std::unexpected {parentWorld.error()};
    }
    if (!isFinite(*parentWorld * validated->second))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform is not representable")};
    }
    slot.local = validated->first;
    slot.localMatrix = validated->second;
    markDirty(*slotIndex);
    return {};
}

auto SceneState::getWorldTransform(const Entity& entity) -> Result<glm::mat4>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    auto updated = updateWorld(*slot);
    if (!updated)
    {
        return std::unexpected {updated.error()};
    }
    return slotAt(*slot).worldMatrix;
}

auto SceneState::setWorldTransform(const Entity& entity, const Transform& transform) -> Result<void>
{
    auto slotIndex = resolve(entity);
    if (!slotIndex)
    {
        return std::unexpected {slotIndex.error()};
    }
    auto world = validateTransform(transform);
    if (!world)
    {
        return std::unexpected {world.error()};
    }
    auto local = preserveWorld(world->second, slotAt(*slotIndex).parent);
    if (!local)
    {
        return std::unexpected {local.error()};
    }
    return setLocalTransform(entity, local->first);
}

auto SceneState::getParent(const Entity& entity) const -> Result<std::optional<Entity>>
{
    auto slotIndex = resolve(entity);
    if (!slotIndex)
    {
        return std::unexpected {slotIndex.error()};
    }
    const auto parent = slotAt(*slotIndex).parent;
    if (!parent)
    {
        return std::optional<Entity> {};
    }
    return std::optional<Entity> {makeEntity(*parent)};
}

auto SceneState::setParent(const Entity& entity, const std::optional<Entity>& parent, ReparentMode mode) -> Result<void>
{
    auto childIndex = resolve(entity);
    if (!childIndex)
    {
        return std::unexpected {childIndex.error()};
    }
    auto parentIndex = resolveParent(parent);
    if (!parentIndex)
    {
        return std::unexpected {parentIndex.error()};
    }
    if ((mode != ReparentMode::KeepLocal && mode != ReparentMode::KeepWorld) ||
        wouldCreateCycle(*childIndex, *parentIndex))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid reparent mode or hierarchy cycle")};
    }
    auto local = localAfterReparent(*childIndex, *parentIndex, mode);
    if (!local)
    {
        return std::unexpected {local.error()};
    }
    attach(*childIndex, *parentIndex, local->first, local->second);
    markDirty(*childIndex);
    return {};
}

auto SceneState::destroyEntity(const Entity& entity) -> Result<void>
{
    auto slot = resolve(entity);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    if (!slotAt(*slot).children.empty())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cannot delete a scene object with children")};
    }
    destroySlot(*slot);
    return {};
}

auto SceneState::destroySubtree(const Entity& entity) -> Result<void>
{
    auto root = resolve(entity);
    if (!root)
    {
        return std::unexpected {root.error()};
    }
    auto pending = std::vector<std::size_t> {*root};
    if (const auto parent = slotAt(*root).parent)
    {
        std::erase(slotAt(*parent).children, *root);
    }
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        auto children = std::move(slotAt(current).children);
        pending.insert(pending.end(), children.begin(), children.end());
        slotAt(current).parent.reset();
        destroySlot(current);
    }
    return {};
}

auto SceneState::resolve(const Entity& entity) const -> Result<std::size_t>
{
    if (!isAlive(entity))
    {
        return std::unexpected {transformError()};
    }
    return entity._slot;
}

auto SceneState::slotAt(std::size_t index) -> Slot&
{
    ::panda::expect(index < _slots.size(), "Scene slot index is out of range");
    return *std::next(_slots.begin(), static_cast<std::ptrdiff_t>(index));
}

auto SceneState::slotAt(std::size_t index) const -> const Slot&
{
    ::panda::expect(index < _slots.size(), "Scene slot index is out of range");
    return *std::next(_slots.begin(), static_cast<std::ptrdiff_t>(index));
}

auto SceneState::resolveParent(const std::optional<Entity>& parent) const -> Result<std::optional<std::size_t>>
{
    if (!parent)
    {
        return std::optional<std::size_t> {};
    }
    auto slot = resolve(*parent);
    if (!slot)
    {
        return std::unexpected {slot.error()};
    }
    return std::optional<std::size_t> {*slot};
}

auto SceneState::localAfterReparent(std::size_t child, std::optional<std::size_t> parent, ReparentMode mode)
    -> Result<std::pair<Transform, glm::mat4>>
{
    if (mode == ReparentMode::KeepLocal)
    {
        return std::pair {slotAt(child).local, slotAt(child).localMatrix};
    }
    auto world = getWorldTransform(makeEntity(child));
    if (!world)
    {
        return std::unexpected {world.error()};
    }
    return preserveWorld(*world, parent);
}

auto SceneState::preserveWorld(const glm::mat4& world, std::optional<std::size_t> parent)
    -> Result<std::pair<Transform, glm::mat4>>
{
    if (parent)
    {
        auto updated = updateWorld(*parent);
        if (!updated)
        {
            return std::unexpected {updated.error()};
        }
        auto inverse = checkedInverse(slotAt(*parent).worldMatrix);
        if (!inverse)
        {
            return std::unexpected {inverse.error()};
        }
        return decomposeTransform(*inverse * world);
    }
    return decomposeTransform(world);
}

auto SceneState::wouldCreateCycle(std::size_t child, std::optional<std::size_t> parent) const -> bool
{
    for (auto ancestor = parent; ancestor; ancestor = slotAt(*ancestor).parent)
    {
        if (*ancestor == child)
        {
            return true;
        }
    }
    return false;
}

void SceneState::attach(std::size_t childIndex,
                        std::optional<std::size_t> parentIndex,
                        const Transform& local,
                        const glm::mat4& matrix)
{
    auto& child = slotAt(childIndex);
    if (child.parent)
    {
        std::erase(slotAt(*child.parent).children, childIndex);
    }
    child.parent = parentIndex;
    if (parentIndex)
    {
        slotAt(*parentIndex).children.push_back(childIndex);
    }
    child.local = local;
    child.localMatrix = matrix;
}

auto SceneState::updateWorld(std::size_t slotIndex) -> Result<void>
{
    auto pending = std::vector<std::size_t> {};
    for (auto current = std::optional {slotIndex}; current && slotAt(*current).dirty; current = slotAt(*current).parent)
    {
        pending.push_back(*current);
    }
    for (const auto current : std::views::reverse(pending))
    {
        auto& slot = slotAt(current);
        const auto parentWorld = slot.parent ? slotAt(*slot.parent).worldMatrix : glm::mat4 {1.0F};
        const auto world = parentWorld * slot.localMatrix;
        if (!isFinite(world))
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform is not representable")};
        }
        slot.worldMatrix = world;
        slot.dirty = false;
    }
    return {};
}

void SceneState::markDirty(std::size_t slotIndex)
{
    auto pending = std::vector<std::size_t> {slotIndex};
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        slotAt(current).dirty = true;
        pending.insert(pending.end(), slotAt(current).children.begin(), slotAt(current).children.end());
    }
}

auto SceneState::getParentWorld(std::optional<std::size_t> parent) -> Result<glm::mat4>
{
    if (!parent)
    {
        return glm::mat4 {1.0F};
    }
    auto updated = updateWorld(*parent);
    if (!updated)
    {
        return std::unexpected {updated.error()};
    }
    return slotAt(*parent).worldMatrix;
}

void SceneState::destroySlot(std::size_t slotIndex)
{
    auto& slot = slotAt(slotIndex);
    if (slot.parent)
    {
        std::erase(slotAt(*slot.parent).children, slotIndex);
    }
    if (slot.objectId)
    {
        const auto objectSlot = _objectSlots.find(*slot.objectId);
        ::panda::expect(objectSlot != _objectSlots.end(), "Scene ObjectId is missing from the object index");
        objectSlot->second.reset();
    }
    if (_activeCamera == slotIndex)
    {
        _activeCamera.reset();
    }
    const auto reuseCount = slot.reuseCount + 1;
    slot = Slot {};
    slot.reuseCount = reuseCount;
    _freeSlots.push_back(slotIndex);
}

auto SceneState::setMesh(const Entity& entity, std::optional<MeshInstance> mesh) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    if (mesh && (!mesh->mesh.isValid() || mesh->mesh.getVertexStride() != standardMeshVertexStrideBytes ||
                 !isFinite(mesh->tint) || glm::any(glm::lessThan(mesh->tint, glm::vec4 {0.0F}))))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Scene mesh or tint is invalid")};
    }
    slotAt(*index).mesh = std::move(mesh);
    return {};
}

auto SceneState::setCamera(const Entity& entity, std::optional<PerspectiveCamera> camera) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    auto validation = validateCamera(camera);
    if (!validation)
    {
        return std::unexpected {validation.error()};
    }
    slotAt(*index).camera = camera;
    if (camera)
    {
        slotAt(*index).orthographicCamera.reset();
    }
    if (!camera && !slotAt(*index).orthographicCamera && _activeCamera == *index)
    {
        _activeCamera.reset();
    }
    return {};
}

auto SceneState::setOrthographicCamera(const Entity& entity, std::optional<OrthographicCamera> camera) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    auto validation = validateCamera(camera);
    if (!validation)
    {
        return std::unexpected {validation.error()};
    }
    slotAt(*index).orthographicCamera = camera;
    if (camera)
    {
        slotAt(*index).camera.reset();
    }
    if (!camera && !slotAt(*index).camera && _activeCamera == *index)
    {
        _activeCamera.reset();
    }
    return {};
}

auto SceneState::setLight(const Entity& entity, std::optional<Light> light) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    if (light && !isValidSceneLight(*light))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Scene light is invalid")};
    }
    slotAt(*index).light = light;
    return {};
}

auto SceneState::setVisible(const Entity& entity, bool visible) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    slotAt(*index).visible = visible;
    return {};
}

auto SceneState::setActiveCamera(const Entity& entity) -> Result<void>
{
    auto index = resolve(entity);
    if (!index)
    {
        return std::unexpected {index.error()};
    }
    if (!slotAt(*index).camera && !slotAt(*index).orthographicCamera)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Active object has no camera")};
    }
    _activeCamera = *index;
    return {};
}

auto SceneState::createRenderView(float aspectRatio) -> Result<RenderView>
{
    return SceneRenderViewBuilder {*this}.build(aspectRatio);
}
}

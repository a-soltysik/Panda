#pragma once

/// @file
/// Optional scene identities, transforms and render-view preparation.

#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <memory>
#include <numbers>
#include <optional>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/ObjectId.hpp>
#include <string>
#include <variant>

namespace panda
{
class Context;
}

namespace panda::scene
{
namespace detail
{
class SceneState;
struct SceneIdentity;
}

/// @brief Initial name and local transform for a scene object.
struct EntityDesc
{
    /// Editable label; names may repeat.
    std::string name;
    /// Initial local translation, rotation and scale.
    Transform transform {};
};

/// @brief Selects which transform remains fixed when changing an object's parent.
enum class ReparentMode : std::uint8_t
{
    /// Preserve the object's local transform.
    KeepLocal,
    /// Preserve the object's world matrix when the result is representable as TRS.
    KeepWorld
};

/// @brief Mesh retained by a scene object; uploading remains the application's responsibility.
struct MeshInstance
{
    /// Standard-format mesh from the scene's Context.
    Mesh mesh;
    /// Nonnegative finite linear RGBA tint.
    glm::vec4 tint {1.0F};
};

/// @brief Perspective lens; pose comes from the object's world transform, looking along local -Z.
struct PerspectiveCamera
{
    /// Vertical field of view in radians.
    float verticalFieldOfViewRadians {1.04719755F};
    /// Positive near distance in meters.
    float nearPlane {0.05F};
    /// Far distance in meters, greater than nearPlane.
    float farPlane {1000.0F};
};

/// @brief Orthographic lens; pose comes from the object's world transform, looking along local -Z.
struct OrthographicCamera
{
    /// Positive vertical span in meters.
    float height {10.0F};
    /// Positive near distance in meters.
    float nearPlane {0.05F};
    /// Far distance in meters, greater than nearPlane.
    float farPlane {1000.0F};
};

/// @brief Directional light shining along the object's local -Z axis.
struct DirectionalLight
{
    /// Nonnegative finite linear RGB color.
    glm::vec3 color {1.0F};
    /// Nonnegative finite intensity multiplier; only the first visible scene light is shadowed.
    float intensity {3.0F};
};

/// @brief Point light whose position comes from the entity's world transform.
struct PointLight
{
    /// Nonnegative finite linear RGB color.
    glm::vec3 color {1.0F};
    /// Nonnegative finite intensity multiplier.
    float intensity {1.0F};
    /// Positive finite distance in meters where the contribution reaches zero.
    float range {};
};

/// @brief Spot light whose position and local -Z direction come from the entity's world transform.
struct SpotLight
{
    /// Nonnegative finite linear RGB color.
    glm::vec3 color {1.0F};
    /// Nonnegative finite intensity multiplier.
    float intensity {1.0F};
    /// Positive finite distance in meters where the contribution reaches zero.
    float range {};
    /// Radians from the light axis where angular falloff begins.
    float innerConeAngleRadians {};
    /// Radians from the light axis where angular falloff reaches zero.
    float outerConeAngleRadians {std::numbers::pi_v<float> / 4.0F};
};

/// @brief One light component attached to a scene entity.
using Light = std::variant<DirectionalLight, PointLight, SpotLight>;

/// @brief Move-only owner of scene objects, transform hierarchy and retained mesh associations.
/// A Scene borrows its Context, which must outlive it. Entities do not retain or
/// dereference a Scene and are resolved only through a live Scene.
/// Calls must be serialized. Object operations reject stale and foreign handles
/// with InvalidArgument. Failed edits leave logical state unchanged; world queries
/// may update derived caches. Mesh uploads remain the application's responsibility.
/// Operations may allocate CPU storage; they do not submit GPU work.
class Scene final
{
public:
    /// @brief Creates an empty Scene associated with a live Context.
    /// @param context Context that must outlive the returned Scene and remain at the same address.
    /// @return An empty scene, or InvalidArgument if the Context is moved-from.
    [[nodiscard]] static auto create(Context& context) -> Result<Scene>;

    Scene(const Scene&) = delete;
    auto operator=(const Scene&) -> Scene& = delete;

    /// @brief Transfers scene identity and objects; existing Entity values remain valid.
    Scene(Scene&& other) noexcept;

    /// @brief Releases previous objects and transfers other's identity; previous handles become stale.
    auto operator=(Scene&& other) noexcept -> Scene&;

    /// @brief Releases scene-owned mesh references and invalidates handles; snapshots retain their meshes.
    ~Scene();

    /// @brief Creates an object with no parent and a validated local transform.
    /// @param description Name and initial transform.
    /// @return A scene-scoped handle, or InvalidArgument when the transform is invalid.
    [[nodiscard]] auto createEntity(const EntityDesc& description = {}) -> Result<Entity>;

    /// @brief Creates an object with a supplied ID, for future scene reconstruction.
    /// @return A scene-scoped handle, or InvalidArgument for an invalid transform or duplicate ID.
    [[nodiscard]] auto createEntity(ObjectId objectId, const EntityDesc& description = {}) -> Result<Entity>;

    /// @brief Returns the stable object identifier for a live object.
    [[nodiscard]] auto getObjectId(const Entity& entity) const -> Result<ObjectId>;

    /// @brief Finds a live object by stable object identifier.
    /// @return The current handle, or empty when the identifier is unknown or its object was deleted.
    [[nodiscard]] auto findEntity(ObjectId objectId) const -> std::optional<Entity>;

    /// @brief Reports whether this Scene currently owns the supplied live handle.
    [[nodiscard]] auto isAlive(const Entity& entity) const noexcept -> bool;

    /// @brief Returns a value snapshot of the object's local TRS.
    [[nodiscard]] auto getLocalTransform(const Entity& entity) const -> Result<Transform>;

    /// @brief Returns the object's editable label.
    [[nodiscard]] auto getName(const Entity& entity) const -> Result<std::string>;

    /// @brief Replaces the object's editable label; names may repeat.
    [[nodiscard]] auto setName(const Entity& entity, std::string name) -> Result<void>;

    /// @brief Validates and atomically replaces the object's local TRS.
    [[nodiscard]] auto setLocalTransform(const Entity& entity, const Transform& transform) -> Result<void>;

    /// @brief Returns the complete cached world matrix, including derived shear.
    [[nodiscard]] auto getWorldTransform(const Entity& entity) -> Result<glm::mat4>;

    /// @brief Sets world space TRS when its local result is representable without shear.
    [[nodiscard]] auto setWorldTransform(const Entity& entity, const Transform& transform) -> Result<void>;

    /// @brief Returns the object's current parent, or an empty optional for a root.
    [[nodiscard]] auto getParent(const Entity& entity) const -> Result<std::optional<Entity>>;

    /// @brief Changes parent while preserving either local or world transform.
    /// @return InvalidArgument for stale/cross-scene handles, cycles, singular parents,
    /// or a keep-world result that requires shear. Failure leaves the hierarchy unchanged.
    [[nodiscard]] auto setParent(const Entity& entity,
                                 const std::optional<Entity>& parent,
                                 ReparentMode mode = ReparentMode::KeepLocal) -> Result<void>;

    /// @brief Deletes an object that has no children.
    [[nodiscard]] auto destroyEntity(const Entity& entity) -> Result<void>;

    /// @brief Deletes an object and all its descendants.
    [[nodiscard]] auto destroySubtree(const Entity& entity) -> Result<void>;

    /// @brief Attaches or removes a retained mesh and tint. Invalid edits leave existing data unchanged.
    [[nodiscard]] auto setMesh(const Entity& entity, std::optional<MeshInstance> mesh) -> Result<void>;

    /// @brief Attaches or removes a validated perspective lens; removing it clears active selection.
    [[nodiscard]] auto setCamera(const Entity& entity, std::optional<PerspectiveCamera> camera) -> Result<void>;

    /// @brief Attaches or removes a validated orthographic lens; setting either lens replaces the other kind.
    [[nodiscard]] auto setOrthographicCamera(const Entity& entity, std::optional<OrthographicCamera> camera)
        -> Result<void>;

    /// @brief Attaches or removes validated light parameters; point uses world position, directional/spot use world rotation.
    [[nodiscard]] auto setLight(const Entity& entity, std::optional<Light> light) -> Result<void>;

    /// @brief Changes rendering visibility of this object alone; children remain independent.
    [[nodiscard]] auto setVisible(const Entity& entity, bool visible) -> Result<void>;

    /// @brief Selects a live object with a camera lens. Deletion/removal clears the selection.
    [[nodiscard]] auto setActiveCamera(const Entity& entity) -> Result<void>;

    /// @brief Copies visible draws, active camera and visible lights into renderer input.
    /// @param aspectRatio Finite positive framebuffer width divided by height, supplied each frame.
    /// @return A snapshot retaining its meshes, or InvalidArgument for a missing camera or invalid pose.
    /// The first visible directional light uses the current shadow pass; point and spot lights are unshadowed.
    /// Context::render validates renderer input and the configured light budget.
    /// Camera/light poses require positive uniform world scale without shear.
    /// Calls are serialized with scene mutation and Context operations; no GPU work is submitted.
    [[nodiscard]] auto createRenderView(float aspectRatio) -> Result<RenderView>;

private:
    explicit Scene(Context& context, std::unique_ptr<detail::SceneState> state) noexcept;

    Context* _context {nullptr};
    std::unique_ptr<detail::SceneState> _state;
};
}

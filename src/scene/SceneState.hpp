#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <panda/scene/Scene.hpp>
#include <string>
#include <utility>
#include <vector>

namespace panda::scene::detail
{
struct SceneIdentity final
{
};

class SceneRenderViewBuilder;

class SceneState final
{
public:
    [[nodiscard]] auto createEntity(const EntityDesc& description) -> Result<Entity>;
    [[nodiscard]] auto createEntity(ObjectId objectId, const EntityDesc& description) -> Result<Entity>;
    [[nodiscard]] auto isAlive(const Entity& entity) const noexcept -> bool;
    [[nodiscard]] auto getObjectId(const Entity& entity) const -> Result<ObjectId>;
    [[nodiscard]] auto findEntity(ObjectId objectId) const -> std::optional<Entity>;
    [[nodiscard]] auto getLocalTransform(const Entity& entity) const -> Result<Transform>;
    [[nodiscard]] auto getName(const Entity& entity) const -> Result<std::string>;
    [[nodiscard]] auto setName(const Entity& entity, std::string name) -> Result<void>;
    [[nodiscard]] auto setLocalTransform(const Entity& entity, const Transform& transform) -> Result<void>;
    [[nodiscard]] auto getWorldTransform(const Entity& entity) -> Result<glm::mat4>;
    [[nodiscard]] auto setWorldTransform(const Entity& entity, const Transform& transform) -> Result<void>;
    [[nodiscard]] auto getParent(const Entity& entity) const -> Result<std::optional<Entity>>;
    [[nodiscard]] auto setParent(const Entity& entity, const std::optional<Entity>& parent, ReparentMode mode)
        -> Result<void>;
    [[nodiscard]] auto destroyEntity(const Entity& entity) -> Result<void>;
    [[nodiscard]] auto destroySubtree(const Entity& entity) -> Result<void>;
    [[nodiscard]] auto setMesh(const Entity& entity, std::optional<MeshInstance> mesh) -> Result<void>;
    [[nodiscard]] auto setCamera(const Entity& entity, std::optional<PerspectiveCamera> camera) -> Result<void>;
    [[nodiscard]] auto setOrthographicCamera(const Entity& entity, std::optional<OrthographicCamera> camera)
        -> Result<void>;
    [[nodiscard]] auto setLight(const Entity& entity, std::optional<Light> light) -> Result<void>;
    [[nodiscard]] auto setVisible(const Entity& entity, bool visible) -> Result<void>;
    [[nodiscard]] auto setActiveCamera(const Entity& entity) -> Result<void>;
    [[nodiscard]] auto createRenderView(float aspectRatio) -> Result<RenderView>;

private:
    struct Slot
    {
        std::string name;
        std::optional<ObjectId> objectId;
        Transform local {};
        glm::mat4 localMatrix {1.0F};
        glm::mat4 worldMatrix {1.0F};
        std::optional<std::size_t> parent;
        std::vector<std::size_t> children;
        std::uint64_t reuseCount {0};
        bool alive {false};
        bool dirty {true};
        bool visible {true};
        std::optional<MeshInstance> mesh;
        std::optional<PerspectiveCamera> camera;
        std::optional<OrthographicCamera> orthographicCamera;
        std::optional<Light> light;
    };

    [[nodiscard]] auto generateObjectId() -> ObjectId;
    [[nodiscard]] auto insertEntity(ObjectId objectId,
                                    const EntityDesc& description,
                                    const std::pair<Transform, glm::mat4>& validatedTransform) -> Result<Entity>;
    [[nodiscard]] auto allocateSlot() -> std::size_t;
    [[nodiscard]] auto resolve(const Entity& entity) const -> Result<std::size_t>;
    [[nodiscard]] auto slotAt(std::size_t index) -> Slot&;
    [[nodiscard]] auto slotAt(std::size_t index) const -> const Slot&;
    [[nodiscard]] auto resolveParent(const std::optional<Entity>& parent) const -> Result<std::optional<std::size_t>>;
    [[nodiscard]] auto localAfterReparent(std::size_t child, std::optional<std::size_t> parent, ReparentMode mode)
        -> Result<std::pair<Transform, glm::mat4>>;
    [[nodiscard]] auto preserveWorld(const glm::mat4& world, std::optional<std::size_t> parent)
        -> Result<std::pair<Transform, glm::mat4>>;
    [[nodiscard]] auto wouldCreateCycle(std::size_t child, std::optional<std::size_t> parent) const -> bool;
    void attach(std::size_t child, std::optional<std::size_t> parent, const Transform& local, const glm::mat4& matrix);
    [[nodiscard]] auto updateWorld(std::size_t slot) -> Result<void>;
    [[nodiscard]] auto getParentWorld(std::optional<std::size_t> parent) -> Result<glm::mat4>;
    void markDirty(std::size_t slot);
    void destroySlot(std::size_t slot);
    [[nodiscard]] auto makeEntity(std::size_t index) const -> Entity;

    friend class SceneRenderViewBuilder;

    std::shared_ptr<const SceneIdentity> _identity {std::make_shared<SceneIdentity>()};
    std::vector<Slot> _slots;
    std::vector<std::size_t> _freeSlots;
    std::map<ObjectId, std::optional<std::size_t>> _objectSlots;
    ObjectId _nextObjectId {1};
    std::optional<std::size_t> _activeCamera;
};
}

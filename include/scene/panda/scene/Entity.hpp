#pragma once

/// @file
/// Runtime handles for scene objects.

#include <cstddef>
#include <cstdint>
#include <memory>

namespace panda::scene
{
class Scene;

namespace detail
{
class SceneState;
struct SceneIdentity;
}

/// @brief Non-owning, scene-scoped identity for one object.
class Entity final
{
public:
    /// @brief Creates an empty handle that does not resolve in any scene.
    Entity();

    /// @brief Compares scene identity, slot and its reuse count.
    [[nodiscard]] auto operator==(const Entity& other) const noexcept -> bool;

private:
    friend class detail::SceneState;
    friend class Scene;

    std::weak_ptr<const detail::SceneIdentity> _scene;
    std::size_t _slot {0};
    std::uint64_t _slotReuseCount {0};
};
}

#include <panda/scene/Entity.hpp>

namespace panda::scene
{
Entity::Entity() = default;

auto Entity::operator==(const Entity& other) const noexcept -> bool
{
    return _slot == other._slot && _slotReuseCount == other._slotReuseCount && !_scene.owner_before(other._scene) &&
           !other._scene.owner_before(_scene);
}
}

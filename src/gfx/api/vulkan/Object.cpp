#include "Object.h"

namespace panda::gfx::vulkan
{

auto Object::createObject() -> Object
{
    static auto currentId = Id {};
    return Object {currentId++};
}

Object::Object(Id newId)
    : _id {newId}
{
}

auto Object::getId() const noexcept -> Id
{
    return _id;
}

}

#include "panda/gfx/vulkan/Object.h"

namespace panda::gfx::vulkan
{

auto Object::getId() const noexcept -> Id
{
    return _id;
}

Object::Object(const std::string& name)
    : _name {name},
      _id {currentId++}
{
}

auto Object::getName() const noexcept -> const std::string&
{
    return _name;
}

auto Transform::mat4() const noexcept -> glm::mat4
{
    const float c3 = glm::cos(rotation.z);
    const float s3 = glm::sin(rotation.z);
    const float c2 = glm::cos(rotation.x);
    const float s2 = glm::sin(rotation.x);
    const float c1 = glm::cos(rotation.y);
    const float s1 = glm::sin(rotation.y);
    return glm::mat4 {
        {
         scale.x * (c1 * c3 + s1 * s2 * s3),
         scale.x * (c2 * s3),
         scale.x * (c1 * s2 * s3 - c3 * s1),
         0.0f, },
        {
         scale.y * (c3 * s1 * s2 - c1 * s3),
         scale.y * (c2 * c3),
         scale.y * (c1 * c3 * s2 + s1 * s3),
         0.0f, },
        {
         scale.z * (c2 * s1),
         scale.z * (-s2),
         scale.z * (c1 * c2),
         0.0f, },
        {translation.x, translation.y, translation.z, 1.0f}
    };
}

auto Transform::normalMatrix() const noexcept -> glm::mat3
{
    const float c3 = glm::cos(rotation.z);
    const float s3 = glm::sin(rotation.z);
    const float c2 = glm::cos(rotation.x);
    const float s2 = glm::sin(rotation.x);
    const float c1 = glm::cos(rotation.y);
    const float s1 = glm::sin(rotation.y);
    const auto invScale = 1.f / scale;
    return glm::mat3 {
        {invScale.x * (c1 * c3 + s1 * s2 * s3), invScale.x * (c2 * s3), invScale.x * (c1 * s2 * s3 - c3 * s1)},
        {invScale.y * (c3 * s1 * s2 - c1 * s3), invScale.y * (c2 * c3), invScale.y * (c1 * c3 * s2 + s1 * s3)},
        {invScale.z * (c2 * s1),                invScale.z * (-s2),     invScale.z * (c1 * c2)               },
    };
}

}

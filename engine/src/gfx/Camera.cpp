#include "panda/gfx/Camera.h"

namespace panda::gfx
{

auto Camera::setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) -> void
{
    _projectionMatrix = glm::mat4 {1.f};
    _projectionMatrix[0][0] = 2.f / (right - left);
    _projectionMatrix[1][1] = 2.f / (bottom - top);
    _projectionMatrix[2][2] = 1.f / (far - near);
    _projectionMatrix[3][0] = -(right + left) / (right - left);
    _projectionMatrix[3][1] = -(bottom + top) / (bottom - top);
    _projectionMatrix[3][2] = -near / (far - near);
}

auto Camera::setPerspectiveProjection(float fovY, float aspect, float near, float far) -> void
{
    const auto tanHalfFovY = glm::tan(fovY / 2.f);
    _projectionMatrix = glm::mat4 {0.f};
    _projectionMatrix[0][0] = 1.f / (aspect * tanHalfFovY);
    _projectionMatrix[1][1] = 1.f / tanHalfFovY;
    _projectionMatrix[2][2] = far / (far - near);
    _projectionMatrix[2][3] = 1.f;
    _projectionMatrix[3][2] = -(far * near) / (far - near);
}

auto Camera::getProjection() const noexcept -> const glm::mat4&
{
    return _projectionMatrix;
}

auto Camera::getView() const noexcept -> const glm::mat4&
{
    return _viewMatrix;
}

auto Camera::setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up) -> void
{
    position.y = -position.y;
    direction.y = -direction.y;

    const auto w = glm::vec3 {glm::normalize(direction)};
    const auto u = glm::vec3 {glm::normalize(glm::cross(w, up))};
    const auto v = glm::vec3 {glm::cross(w, u)};

    _viewMatrix = glm::mat4 {1.f};
    _viewMatrix[0][0] = u.x;
    _viewMatrix[1][0] = u.y;
    _viewMatrix[2][0] = u.z;
    _viewMatrix[0][1] = v.x;
    _viewMatrix[1][1] = v.y;
    _viewMatrix[2][1] = v.z;
    _viewMatrix[0][2] = w.x;
    _viewMatrix[1][2] = w.y;
    _viewMatrix[2][2] = w.z;
    _viewMatrix[3][0] = -glm::dot(u, position);
    _viewMatrix[3][1] = -glm::dot(v, position);
    _viewMatrix[3][2] = -glm::dot(w, position);

    _inverseViewMatrix = glm::mat4{1.f};
    _inverseViewMatrix[0][0] = u.x;
    _inverseViewMatrix[0][1] = u.y;
    _inverseViewMatrix[0][2] = u.z;
    _inverseViewMatrix[1][0] = v.x;
    _inverseViewMatrix[1][1] = v.y;
    _inverseViewMatrix[1][2] = v.z;
    _inverseViewMatrix[2][0] = w.x;
    _inverseViewMatrix[2][1] = w.y;
    _inverseViewMatrix[2][2] = w.z;
    _inverseViewMatrix[3][0] = position.x;
    _inverseViewMatrix[3][1] = position.y;
    _inverseViewMatrix[3][2] = position.z;
}

auto Camera::setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up) -> void
{
    setViewDirection(position, target - position, up);
}

auto Camera::setViewYXZ(glm::vec3 position, glm::vec3 rotation) -> void
{
    position.y = -position.y;

    const auto c3 = glm::cos(rotation.z);
    const auto s3 = glm::sin(rotation.z);
    const auto c2 = glm::cos(rotation.x);
    const auto s2 = glm::sin(rotation.x);
    const auto c1 = glm::cos(rotation.y);
    const auto s1 = glm::sin(rotation.y);
    const auto u = glm::vec3 {(c1 * c3 + s1 * s2 * s3), (c2 * s3), (c1 * s2 * s3 - c3 * s1)};
    const auto v = glm::vec3 {(c3 * s1 * s2 - c1 * s3), (c2 * c3), (c1 * c3 * s2 + s1 * s3)};
    const auto w = glm::vec3 {(c2 * s1), (-s2), (c1 * c2)};
    _viewMatrix = glm::mat4 {1.f};
    _viewMatrix[0][0] = u.x;
    _viewMatrix[1][0] = u.y;
    _viewMatrix[2][0] = u.z;
    _viewMatrix[0][1] = v.x;
    _viewMatrix[1][1] = v.y;
    _viewMatrix[2][1] = v.z;
    _viewMatrix[0][2] = w.x;
    _viewMatrix[1][2] = w.y;
    _viewMatrix[2][2] = w.z;
    _viewMatrix[3][0] = -glm::dot(u, position);
    _viewMatrix[3][1] = -glm::dot(v, position);
    _viewMatrix[3][2] = -glm::dot(w, position);

    _inverseViewMatrix = glm::mat4{1.f};
    _inverseViewMatrix[0][0] = u.x;
    _inverseViewMatrix[0][1] = u.y;
    _inverseViewMatrix[0][2] = u.z;
    _inverseViewMatrix[1][0] = v.x;
    _inverseViewMatrix[1][1] = v.y;
    _inverseViewMatrix[1][2] = v.z;
    _inverseViewMatrix[2][0] = w.x;
    _inverseViewMatrix[2][1] = w.y;
    _inverseViewMatrix[2][2] = w.z;
    _inverseViewMatrix[3][0] = position.x;
    _inverseViewMatrix[3][1] = position.y;
    _inverseViewMatrix[3][2] = position.z;
}

auto Camera::getInverseView() const noexcept -> const glm::mat4&
{
    return _inverseViewMatrix;
}

}

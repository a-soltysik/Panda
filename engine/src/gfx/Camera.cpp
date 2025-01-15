#include "panda/gfx/Camera.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/trigonometric.hpp>

namespace panda::gfx
{

auto Camera::setOrthographicProjection(const projection::Orthographic& projection) -> void
{
    _projectionMatrix = glm::mat4 {1.F};
    _projectionMatrix[0][0] = 2.F / (projection.right - projection.left);
    _projectionMatrix[1][1] = 2.F / (projection.bottom - projection.top);
    _projectionMatrix[2][2] = 1.F / (projection.far - projection.near);
    _projectionMatrix[3][0] = -(projection.right + projection.left) / (projection.right - projection.left);
    _projectionMatrix[3][1] = -(projection.bottom + projection.top) / (projection.bottom - projection.top);
    _projectionMatrix[3][2] = -projection.near / (projection.far - projection.near);
}

auto Camera::setPerspectiveProjection(const projection::Perspective& projection) -> void
{
    const auto tanHalfFovY = glm::tan(projection.fovY / 2.F);
    _projectionMatrix = glm::mat4 {0.F};
    _projectionMatrix[0][0] = 1.F / (projection.aspect * tanHalfFovY);
    _projectionMatrix[1][1] = 1.F / tanHalfFovY;
    _projectionMatrix[2][2] = projection.far / (projection.far - projection.near);
    _projectionMatrix[2][3] = 1.F;
    _projectionMatrix[3][2] = -(projection.far * projection.near) / (projection.far - projection.near);
}

auto Camera::getProjection() const noexcept -> const glm::mat4&
{
    return _projectionMatrix;
}

auto Camera::getView() const noexcept -> const glm::mat4&
{
    return _viewMatrix;
}

auto Camera::setViewDirection(const view::Direction& view) -> void
{
    const auto position = glm::vec3 {view.position.x, -view.position.y, view.position.z};
    const auto direction = glm::vec3 {view.direction.x, -view.direction.y, view.direction.z};

    const auto w = glm::vec3 {glm::normalize(direction)};
    const auto u = glm::vec3 {glm::normalize(glm::cross(w, view.up))};
    const auto v = glm::vec3 {glm::cross(w, u)};

    _viewMatrix = glm::mat4 {1.F};
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

    _inverseViewMatrix = glm::mat4 {1.F};
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

auto Camera::setViewTarget(const view::Target& view) -> void
{
    setViewDirection({.position = view.position, .direction = view.target - view.position, .up = view.up});
}

auto Camera::setViewYXZ(const view::YXZ& view) -> void
{
    const auto position = glm::vec3 {view.position.x, -view.position.y, view.position.z};

    const auto c3 = glm::cos(view.rotation.z);
    const auto s3 = glm::sin(view.rotation.z);
    const auto c2 = glm::cos(view.rotation.x);
    const auto s2 = glm::sin(view.rotation.x);
    const auto c1 = glm::cos(view.rotation.y);
    const auto s1 = glm::sin(view.rotation.y);
    const auto u = glm::vec3 {((c1 * c3) + (s1 * s2 * s3)), (c2 * s3), ((c1 * s2 * s3) - (c3 * s1))};
    const auto v = glm::vec3 {((c3 * s1 * s2) - (c1 * s3)), (c2 * c3), ((c1 * c3 * s2) + (s1 * s3))};
    const auto w = glm::vec3 {(c2 * s1), (-s2), (c1 * c2)};
    _viewMatrix = glm::mat4 {1.F};
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

    _inverseViewMatrix = glm::mat4 {1.F};
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

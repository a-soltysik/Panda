#include "Camera.h"

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

auto Camera::setPerspectiveProjection(float fovY, float aspect, float near, float far) -> void {
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

}

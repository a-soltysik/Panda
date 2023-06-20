#include "Camera.h"

namespace panda::gfx
{

auto Camera::setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) -> void
{
    projectionMatrix = glm::mat4 {1.f};
    projectionMatrix[0][0] = 2.f / (right - left);
    projectionMatrix[1][1] = 2.f / (bottom - top);
    projectionMatrix[2][2] = 1.f / (far - near);
    projectionMatrix[3][0] = -(right + left) / (right - left);
    projectionMatrix[3][1] = -(bottom + top) / (bottom - top);
    projectionMatrix[3][2] = -near / (far - near);
}

auto Camera::setPerspectiveProjection(float fovY, float aspect, float near, float far) -> void {
    const auto tanHalfFovY = glm::tan(fovY / 2.f);
    projectionMatrix = glm::mat4 {0.f};
    projectionMatrix[0][0] = 1.f / (aspect * tanHalfFovY);
    projectionMatrix[1][1] = 1.f / tanHalfFovY;
    projectionMatrix[2][2] = far / (far - near);
    projectionMatrix[2][3] = 1.f;
    projectionMatrix[3][2] = -(far * near) / (far - near);
}

auto Camera::getProjection() const noexcept -> const glm::mat4&
{
    return projectionMatrix;
}

}

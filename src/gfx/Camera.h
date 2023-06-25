#pragma once

#include <glm/glm.hpp>

namespace panda::gfx
{

class Camera
{
public:
    auto setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) -> void;
    auto setPerspectiveProjection(float fovY, float aspect, float near, float far) -> void;

    [[nodiscard]] auto getProjection() const noexcept -> const glm::mat4&;

private:
    glm::mat4 _projectionMatrix {1.f};
};

}

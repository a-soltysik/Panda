#pragma once

#include <glm/glm.hpp>

namespace panda::gfx
{

class Camera
{
public:
    auto setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) -> void;
    auto setPerspectiveProjection(float fovY, float aspect, float near, float far) -> void;
    auto setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = glm::vec3 {0.f, -1.f, 0.f}) -> void;
    auto setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up = glm::vec3 {0.f, -1.f, 0.f}) -> void;
    auto setViewYXZ(glm::vec3 position, glm::vec3 rotation) -> void;

    [[nodiscard]] auto getProjection() const noexcept -> const glm::mat4&;
    [[nodiscard]] auto getView() const noexcept -> const glm::mat4&;

private:
    glm::mat4 _projectionMatrix {1.f};
    glm::mat4 _viewMatrix {1.f};
};

}

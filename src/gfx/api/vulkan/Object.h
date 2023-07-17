#pragma once

#include <glm/gtc/matrix_transform.hpp>

#include "Model.h"

namespace panda::gfx::vulkan
{

struct Transform
{
    glm::vec3 translation {};
    glm::vec3 scale {1.f, 1.f, 1.f};
    glm::vec3 rotation {};

    [[nodiscard]] auto mat4() const noexcept -> glm::mat4;
    [[nodiscard]] auto normalMatrix() const noexcept -> glm::mat3;
};

class Object
{
public:
    using Id = size_t;

    [[nodiscard]] static auto createObject() -> Object;

    PD_MOVE_ONLY(Object);
    ~Object() noexcept = default;

    [[nodiscard]] auto getId() const noexcept -> Id;

    Transform transform;
    glm::vec3 color {};
    Model* mesh {};

private:
    explicit Object(Id newId);

    Id _id;
};

}

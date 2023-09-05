#pragma once

#include "panda/gfx/vulkan/Mesh.h"

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

    Object() noexcept;
    PD_MOVE_ONLY(Object);
    ~Object() noexcept = default;

    [[nodiscard]] auto getId() const noexcept -> Id;

    Transform transform;
    glm::vec3 color {};
    Mesh* mesh {};

private:
    inline static Id currentId = 0;
    Id _id;
};

}

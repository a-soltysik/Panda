#pragma once

#include "panda/gfx/Light.h"
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

    explicit Object(const std::string& name);
    PD_MOVE_ONLY(Object);
    ~Object() noexcept = default;

    [[nodiscard]] auto getId() const noexcept -> Id;
    [[nodiscard]] auto getName() const noexcept -> const std::string&;

    Transform transform;
    Mesh* mesh {};

private:
    inline static Id currentId = 0;
    std::string _name;
    Id _id;
};

}

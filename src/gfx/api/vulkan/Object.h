#pragma once

#include "Model.h"

namespace panda::gfx::vulkan
{

struct Transform2
{
    glm::vec2 translation{};
    glm::vec2 scale{1.f, 1.f};
    float rotation{};
    [[nodiscard]] auto mat2() const -> glm::mat2{
        const auto scaleMat = glm::mat2{{scale.x, 0.f}, {0.f, scale.y}};
        const auto rotationMat = glm::mat2{{glm::cos(rotation), glm::sin(rotation)}, {-glm::sin(rotation), glm::cos(rotation)}};
        return rotationMat * scaleMat;
    }
};

class Object
{
public:
    using Id = size_t;

    [[nodiscard]] static auto createObject() -> Object;

    PD_MOVE_ONLY(Object);
    ~Object() noexcept = default;

    Model* mesh;
    Transform2 transform;
    glm::vec3 color;

private:
    explicit Object(Id id);

    Id id;
};

}

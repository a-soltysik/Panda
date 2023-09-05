#pragma once

#include <variant>

namespace panda::gfx
{

struct DirectionalLight
{
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
};

struct PointLight
{
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
};

using Light = std::variant<DirectionalLight, PointLight>;

}
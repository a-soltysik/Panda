#pragma once

#include <glm/vec3.hpp>

namespace panda::gfx
{

struct DirectionalLight
{
    glm::vec3 direction;
    glm::vec4 diffuseColor;
    glm::vec4 ambientColor;
};

struct PointLight
{
    glm::vec3 position;
    glm::vec4 diffuseColor;
    glm::vec4 ambientColor;
};

}
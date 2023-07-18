#pragma once

#include <glm/vec3.hpp>

namespace panda::gfx
{

struct DirectionalLight
{
    glm::vec3 direction;
    glm::vec3 diffuseColor;
    glm::vec3 ambientColor;
};

}
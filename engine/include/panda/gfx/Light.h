#pragma once

#include "panda/Common.h"

namespace panda::gfx
{

struct Attenuation
{
    float constant;
    float linear;
    float exp;
};

struct BaseLight
{
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float intensity;
};

struct DirectionalLight : public BaseLight
{
    glm::vec3 direction;
};

struct PointLight : public BaseLight
{
    glm::vec3 position;
    Attenuation attenuation;
};

struct SpotLight : public PointLight
{
    glm::vec3 direction;
    float cutOff;
};

struct Material
{
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float shininess;
};

constexpr auto makeColorLight(glm::vec3 color, float ambient, float diffuse, float specular, float intensity = 1.f)
    -> BaseLight
{
    return {color * ambient, color * diffuse, color * specular, intensity};
}

constexpr auto makeColorMaterial(glm::vec3 color, float ambient, float diffuse, float specular, float shininess = 1.f)
    -> Material
{
    return {color * ambient, color * diffuse, color * specular, shininess};
}

struct Lights
{
    std::vector<DirectionalLight> directionalLights;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
};

}
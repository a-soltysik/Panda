#pragma once

#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

namespace panda::gfx
{

inline constexpr auto maxLights = size_t {5};

struct Attenuation
{
    float constant;
    float linear;
    float exp;
};

struct BaseLight
{
    std::string name;
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

inline auto makeColorLight(const std::string& name,
                           glm::vec3 color,
                           float ambient,
                           float diffuse,
                           float specular,
                           float intensity = 1.F) -> BaseLight
{
    return {name, color * ambient, color * diffuse, color * specular, intensity};
}

struct Lights
{
    std::vector<DirectionalLight> directionalLights;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
};

}
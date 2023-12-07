struct Attenuation
{
    float constant;
    float linear;
    float exp;
};

struct BaseLight
{
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float intensity;
};

struct DirectionalLight
{
    BaseLight base;
    vec3 direction;
};

struct PointLight
{
    BaseLight base;
    Attenuation attenuation;
    vec3 position;
};

struct SpotLight
{
    PointLight base;
    vec3 direction;
    float cutOff;
};

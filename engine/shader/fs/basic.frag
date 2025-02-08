#version 450

#include "lightUtils.glsl"

layout (location = 0) in vec3 fragWorldPosition;
layout (location = 1) in vec3 fragNormalWorld;
layout (location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout (set = 0, binding = 1) uniform GlobalUbo
{
    mat4 inverseView;

    PointLight pointLights[5];
    DirectionalLight directionalLights[5];
    SpotLight spotLights[5];

    vec3 globalAmbient;
    uint activePointLights;
    uint activeDirectionalLights;
    uint activeSpotLights;
} ubo;

layout (binding = 2) uniform sampler2D texSampler;

vec3 calculateLight(BaseLight light, vec3 lightDirection, vec3 normal)
{
    float lambertian = max(dot(lightDirection, normal), 0.0);
    float specularValue = 0.0;

    if (lambertian > 0.0)
    {
        vec3 cameraWorldPosition = ubo.inverseView[3].xyz;
        vec3 viewDirection = normalize(cameraWorldPosition - fragWorldPosition);
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float specularAngle = max(dot(halfDirection, normal), 0.0);
        specularValue = pow(specularAngle, 64.0);
    }

    vec3 ambient = light.ambient;
    vec3 diffuse = light.diffuse * lambertian * light.intensity;
    vec3 specular = light.specular  * specularValue * light.intensity;

    return ambient + diffuse + specular;
}

vec3 calculatePointLight(PointLight light, vec3 normal)
{
    vec3 lightDirection = light.position - fragWorldPosition;
    float distance = length(lightDirection);
    lightDirection = normalize(lightDirection);

    float attenuation = light.attenuation.constant -
    light.attenuation.linear * distance -
    light.attenuation.exp * distance * distance;

    return calculateLight(light.base, normalize(lightDirection), normal) * attenuation;
}

vec3 calculateDirectionalLight(DirectionalLight light, vec3 normal)
{
    return calculateLight(light.base, normalize(light.direction), normal);
}

vec3 calculateSpotLight(SpotLight light, vec3 normal)
{
    vec3 lightToPixel = normalize(fragWorldPosition - light.base.position);
    float spotFactor = dot(lightToPixel, normalize(light.direction));

    if (spotFactor > light.cutOff)
    {
        vec3 color = calculatePointLight(light.base, normal);
        float spotLightIntensity = (1.0 - (1.0 - spotFactor)/(1.0 - light.cutOff));
        return color * spotLightIntensity;
    }
    else
    {
        return light.base.base.ambient;
    }
}

void main() {
    vec3 totalLight = vec3(0.0);
    vec3 normal = normalize(fragNormalWorld);

    for (uint i = 0; i < ubo.activeDirectionalLights; i++)
    {
        totalLight += calculateDirectionalLight(ubo.directionalLights[i], normal);
    }
    for (uint i = 0; i < ubo.activePointLights; i++)
    {
        totalLight += calculatePointLight(ubo.pointLights[i], normal);
    }
    for (uint i = 0; i < ubo.activeSpotLights; i++)
    {
        totalLight += calculateSpotLight(ubo.spotLights[i], normal);
    }

    outColor = texture(texSampler, fragTexCoord) * vec4(totalLight, 1.0);
}

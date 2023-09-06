#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragWorldPosition;
layout (location = 2) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

struct DirectionalLight
{
    vec4 direction;
    vec4 color;
};

struct PointLight
{
    vec4 position;
    vec4 color;
};

layout (set = 0, binding = 0) uniform GlobalUbo
{
    mat4 projection;
    mat4 view;
    mat4 inverseView;

    vec4 ambientColor;
    PointLight pointLights[6];
    DirectionalLight directionalLights[6];
    uint activePointLights;
    uint activeDirectionalLights;
} ubo;


layout (push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec3 lightSum = ubo.ambientColor.xyz * ubo.ambientColor.w;
    vec3 surfaceNormal = normalize(fragNormalWorld);
    vec3 specular = vec3(0.0);
    vec3 cameraWorldPosition = ubo.inverseView[3].xyz;
    vec3 viewDirection = normalize(cameraWorldPosition - fragWorldPosition);

    for (uint i = 0; i < ubo.activeDirectionalLights; i++)
    {
        DirectionalLight light = ubo.directionalLights[i];
        float intensity = max(dot(fragNormalWorld, normalize(light.direction.xyz)), 0.0);
        lightSum += light.color.xyz * light.color.w * intensity;
    }

    for (uint i = 0; i < ubo.activePointLights; i++)
    {
        PointLight light = ubo.pointLights[i];
        vec3 direction = light.position.xyz - fragWorldPosition;
        float attenuation = 1.0 / dot(direction, direction);
        direction = normalize(direction);
        float incidence = max(dot(surfaceNormal, direction), 0.0);

        lightSum += light.color.xyz * light.color.w * attenuation * incidence;
        vec3 halfAngle = normalize(direction + viewDirection);
        float blinnTerm = dot(surfaceNormal, halfAngle);
        blinnTerm = clamp(blinnTerm, 0.0, 1.0);
        blinnTerm = pow(blinnTerm, 128.0);
        specular += light.color.xyz * light.color.w * attenuation * blinnTerm;
    }

    outColor = vec4((lightSum + specular) * fragColor, 1.0);
}
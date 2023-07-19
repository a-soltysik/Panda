#version 450

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) out vec3 fragColor;

layout (set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionViewMatrix;

    vec3 dDirection;
    vec4 dDiffuse;
    vec4 dAmbient;

    vec3 pPosition;
    vec4 pDiffuse;
    vec4 pAmbient;
} ubo;

layout (push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec4 worldPosition = push.modelMatrix * vec4(position, 1.0);
    gl_Position = ubo.projectionViewMatrix * push.modelMatrix * vec4(position, 1.0);

    vec3 normalWorldSpace = normalize(mat3(push.normalMatrix) * normal);

    float dLightIntensity = max(dot(normalWorldSpace, normalize(ubo.dDirection)), 0.0);
    vec3 dDiffuseLight = ubo.dDiffuse.xyz * ubo.dDiffuse.w * dLightIntensity;
    vec3 dAmbientLight = ubo.dAmbient.xyz * ubo.dAmbient.w;
    vec3 dLightColor = dDiffuseLight + dAmbientLight;


    vec3 pDirectionToLight = ubo.pPosition - worldPosition.xyz;
    float attenuation = 1.0 / dot(pDirectionToLight, pDirectionToLight);

    vec3 pDiffuseLight = ubo.pDiffuse.xyz * ubo.pDiffuse.w * attenuation;
    vec3 pAmbientLight = ubo.pAmbient.xyz * ubo.pAmbient.w;
    pDiffuseLight = pDiffuseLight * max(dot(normalWorldSpace, normalize(pDirectionToLight)), 0.0);
    vec3 pLightColor = pDiffuseLight + pAmbientLight;


    fragColor = (dLightColor + pLightColor) * color;
}

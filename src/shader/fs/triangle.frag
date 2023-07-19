#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragWorldPosition;
layout (location = 2) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

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
    float dLightIntensity = max(dot(fragNormalWorld, normalize(ubo.dDirection)), 0.0);
    vec3 dDiffuseLight = ubo.dDiffuse.xyz * ubo.dDiffuse.w * dLightIntensity;
    vec3 dAmbientLight = ubo.dAmbient.xyz * ubo.dAmbient.w;
    vec3 dLightColor = dDiffuseLight + dAmbientLight;


    vec3 pDirectionToLight = ubo.pPosition - fragWorldPosition;
    float attenuation = 1.0 / dot(pDirectionToLight, pDirectionToLight);

    vec3 pDiffuseLight = ubo.pDiffuse.xyz * ubo.pDiffuse.w * attenuation;
    vec3 pAmbientLight = ubo.pAmbient.xyz * ubo.pAmbient.w;
    pDiffuseLight = pDiffuseLight * max(dot(normalize(fragNormalWorld), normalize(pDirectionToLight)), 0.0);
    vec3 pLightColor = pDiffuseLight + pAmbientLight;


    outColor = vec4((dLightColor + pLightColor) * fragColor, 1.0);
}

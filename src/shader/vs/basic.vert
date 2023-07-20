#version 450

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) out vec3 fragColor;
layout (location = 1) out vec3 fragWorldPosition;
layout (location = 2) out vec3 fragNormalWorld;

layout (set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;

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
    gl_Position = ubo.projection * (ubo.view * worldPosition);

    fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
    fragWorldPosition = worldPosition.xyz;
    fragColor = color;
}

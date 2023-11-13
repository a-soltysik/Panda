#version 450

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) out vec3 fragColor;
layout (location = 1) out vec3 fragWorldPosition;
layout (location = 2) out vec3 fragNormalWorld;
layout (location = 3) out vec2 fragTexCoord;

layout (set = 0, binding = 0) uniform VertUbo
{
    mat4 projection;
    mat4 view;
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
    fragTexCoord = uv;
}

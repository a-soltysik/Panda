#version 450

#include "utils.glsl"

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;

layout (location = 0) out vec3 fragWorldPosition;
layout (location = 1) out vec3 fragNormalWorld;
layout (location = 2) out vec2 fragTexCoord;

layout (set = 0, binding = 0) uniform VertUbo
{
    mat4 projection;
    mat4 view;
} ubo;

struct InstanceData {
    vec3 translation;
    vec3 scale;
    vec3 rotation;
};

layout (set = 0, binding = 3) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

void main() {
    InstanceData instance = instances[gl_InstanceIndex];

    mat3 rotationMatrix = quatToMat3(eulerToQuat(instance.rotation));
    mat4 modelMatrix = mat4(
    vec4(rotationMatrix[0] * instance.scale.x, 0.0),
    vec4(rotationMatrix[1] * instance.scale.y, 0.0),
    vec4(rotationMatrix[2] * instance.scale.z, 0.0),
    vec4(instance.translation, 1.0)
    );

    vec4 worldPosition = modelMatrix * vec4(position, 1.0);
    gl_Position = ubo.projection * (ubo.view * worldPosition);

    fragNormalWorld = normalize(rotationMatrix * normal);
    fragWorldPosition = worldPosition.xyz;
    fragTexCoord = uv;
}

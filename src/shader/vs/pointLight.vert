#version 450

const vec2 OFFSETS[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0)
);

layout (location = 0) out vec2 fragOffset;

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

const float LIGHT_RADIUS = 0.1;

void main() {
    fragOffset = OFFSETS[gl_VertexIndex];

    vec4 lightInCameraSpace = ubo.view * vec4(ubo.pPosition, 1.0);
    vec4 positionInCameraSpace = lightInCameraSpace + LIGHT_RADIUS * vec4(fragOffset, 0.0, 0.0);

    gl_Position = ubo.projection * positionInCameraSpace;
}

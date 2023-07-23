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

    vec4 ambientColor;
    PointLight pointLights[6];
    DirectionalLight directionalLights[6];
    uint activePointLights;
    uint activeDirectionalLights;
} ubo;

layout (push_constant) uniform Push {
    vec4 position;
    vec4 color;
    float radius;
    bool isPointLight;
} push;

void main() {
    if (push.isPointLight)
    {
        fragOffset = OFFSETS[gl_VertexIndex];

        vec4 lightInCameraSpace = ubo.view * push.position;
        vec4 positionInCameraSpace = lightInCameraSpace + push.radius * vec4(fragOffset, 0.0, 0.0);

        gl_Position = ubo.projection * positionInCameraSpace;
    }
}

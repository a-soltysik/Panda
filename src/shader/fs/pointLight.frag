#version 450

layout (location = 0) in vec2 fragOffset;
layout (location = 0) out vec4 outColor;

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
    if (!push.isPointLight) {
        discard;
    }
    if (sqrt(dot(fragOffset, fragOffset)) > 1.0) {
        discard;
    }
    outColor = vec4(push.color.xyz, 1.0);
}
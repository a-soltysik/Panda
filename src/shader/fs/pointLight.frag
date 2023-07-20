#version 450

layout (location = 0) in vec2 fragOffset;
layout (location = 0) out vec4 outColor;

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

void main() {
    if (sqrt(dot(fragOffset, fragOffset)) > 1.0) {
        discard;
    }
    outColor = vec4(ubo.pDiffuse.xyz, 1.0);
}
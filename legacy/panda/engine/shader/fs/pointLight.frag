#version 450

#include "lightUtils.glsl"

layout (location = 0) in vec2 fragOffset;
layout (location = 0) out vec4 outColor;

layout (push_constant) uniform Push {
    vec4 position;
    vec4 color;
    float radius;
} push;

void main() {
    if (sqrt(dot(fragOffset, fragOffset)) > 1.0) {
        discard;
    }
    outColor = vec4(push.color.xyz, 1.0);
}
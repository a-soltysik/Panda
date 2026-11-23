#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(constant_id = 11) const uint testMode = 1u;

layout(set = 2, binding = 3, std140) uniform TestParameters
{
    vec4 tint;
    mat4 transform;
} parameters;

layout(location = 2) in vec4 vertexTint;
layout(location = 0) out vec4 color;

void main()
{
    color = vertexTint * parameters.tint * testColor * float(testMode);
}

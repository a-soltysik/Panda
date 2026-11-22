#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 2, binding = 3, std140) uniform TestParameters
{
    vec4 tint;
    mat4 transform;
} parameters;

layout(push_constant) uniform DrawSettings
{
    vec4 offset;
} drawSettings;

void main()
{
    gl_Position = parameters.transform * (testPosition * parameters.tint) + drawSettings.offset;
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 6) in vec4 testPosition;

layout(set = 2, binding = 3, std140) uniform TestParameters
{
    vec4 tint;
    mat4 transform;
} parameters;

layout(push_constant) uniform DrawSettings
{
    layout(offset = 16) vec4 offset;
} drawSettings;

layout(location = 2) out vec4 vertexTint;

void main()
{
    vertexTint = parameters.tint;
    gl_Position = parameters.transform * testPosition + drawSettings.offset;
}

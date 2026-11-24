#version 450

layout(location = 0) flat in uint instanceIndex;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 3, std430) readonly buffer InstanceColors
{
    vec4 values[];
} instanceColors;

void main()
{
    color = instanceColors.values[instanceIndex];
}

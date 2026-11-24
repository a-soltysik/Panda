#version 450

layout(set = 2, binding = 3, std430) readonly buffer TestStorage
{
    vec4 center;
    vec4 tint;
} storageData;

void main()
{
    gl_Position = storageData.center;
}

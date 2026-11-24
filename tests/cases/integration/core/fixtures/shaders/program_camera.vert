#version 450

layout(set = 2, binding = 3, std430) readonly buffer TestStorage
{
    vec4 position;
    vec4 color;
} storageData;

layout(set = 2, binding = 4, std140) uniform ViewOffset
{
    vec4 translation;
} viewOffset;

void main()
{
    gl_Position = storageData.position + viewOffset.translation;
}

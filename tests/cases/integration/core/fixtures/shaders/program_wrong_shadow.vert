#version 450

layout(set = 2, binding = 3, std430) readonly buffer TestStorage
{
    uvec4 position;
    vec4 color;
} storageData;

void main()
{
    gl_Position = vec4(storageData.position);
}

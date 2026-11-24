#version 450

layout(set = 2, binding = 3, std430) buffer TestStorage
{
    readonly vec4 position;
    vec4 color;
} storageData;

layout(location = 0) out vec4 outputColor;

void main()
{
    outputColor = storageData.position + storageData.color;
}

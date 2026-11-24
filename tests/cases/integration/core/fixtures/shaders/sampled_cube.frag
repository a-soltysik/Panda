#version 450

layout(set = 2, binding = 3) uniform samplerCube imageData;
layout(location = 0) out vec4 outputColor;

void main()
{
    outputColor = texture(imageData, vec3(1.0, 0.0, 0.0));
}

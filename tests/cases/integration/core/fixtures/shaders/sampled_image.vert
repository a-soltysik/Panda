#version 450

layout(set = 2, binding = 3) uniform sampler2D imageData;

void main()
{
    gl_Position = textureLod(imageData, vec2(0.5), 0.0);
}

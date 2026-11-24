#version 450

layout(set = 2, binding = 3) uniform sampler2D applicationImage;
layout(location = 0) out vec4 outputColor;

void main()
{
    outputColor = texture(applicationImage, vec2(0.5)).bgra;
}

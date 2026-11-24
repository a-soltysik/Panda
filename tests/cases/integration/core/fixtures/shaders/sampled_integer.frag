#version 450

layout(set = 2, binding = 3) uniform isampler2D imageData;
layout(location = 0) out vec4 outputColor;

void main()
{
    outputColor = vec4(texelFetch(imageData, ivec2(0), 0));
}

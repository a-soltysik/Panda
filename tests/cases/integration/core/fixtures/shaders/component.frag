#version 450

layout(location = 0, component = 1) in float componentValue;
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(componentValue);
}

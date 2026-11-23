#version 450

layout(set = 1, binding = 0) uniform sampler2D maps[1];
layout(location = 2) in vec4 vertexTint;
layout(location = 0) out vec4 color;

void main()
{
    color = texture(maps[0], vec2(0.0)) * vertexTint;
}

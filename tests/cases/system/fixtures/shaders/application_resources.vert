#version 450

layout(location = 3) in vec3 vertexPosition;

void main()
{
    gl_Position = vec4(vertexPosition.xy, 0.5, 1.0);
}

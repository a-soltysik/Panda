#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vertexPosition;
layout(location = 4) in vec4 vertexColor;

#include "mesh_vertex_common.glsl"

void main()
{
    writeVertex(vertexPosition, vec3(0.0, 0.0, 1.0), vertexColor);
}

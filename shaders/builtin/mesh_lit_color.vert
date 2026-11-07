#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 4) in vec4 vertexColor;

#include "mesh_vertex_common.glsl"

void main()
{
    writeVertex(vertexPosition, vertexNormal, vertexColor);
}

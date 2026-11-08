#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderInterface.inc"

layout(location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION) in vec3 vertexPosition;
layout(location = PANDA_VERTEX_ATTRIBUTE_NORMAL_LOCATION) in vec3 vertexNormal;
layout(location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION) in vec4 vertexColor;
#ifdef HAS_TEXTURE_COORDINATES
layout(location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION) in vec2 vertexTextureCoordinates;
#define BASE_COLOR_TEXTURE_COORDINATES vertexTextureCoordinates
#else
#define BASE_COLOR_TEXTURE_COORDINATES vec2(0.0)
#endif

#include "mesh_vertex_common.glsl"

void main()
{
    writeVertex(vertexPosition, vertexNormal, vertexColor, BASE_COLOR_TEXTURE_COORDINATES);
}

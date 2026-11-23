#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 vertexPosition;
layout(location = 4) in vec4 vertexColor;
#ifdef HAS_TEXTURE_COORDINATES
layout(location = 2) in vec2 vertexTextureCoordinates;
#define BASE_COLOR_TEXTURE_COORDINATES vertexTextureCoordinates
#else
#define BASE_COLOR_TEXTURE_COORDINATES vec2(0.0)
#endif

#include "mesh_vertex_common.glsl"

void main()
{
    writeVertex(vertexPosition, vec3(0.0, 0.0, 1.0), vertexColor, BASE_COLOR_TEXTURE_COORDINATES);
}

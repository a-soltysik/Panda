#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
#ifdef HAS_NORMAL_MAP
layout(location = 3) in vec4 vertexTangent;
#endif
#ifdef HAS_TEXTURE_COORDINATES
layout(location = 2) in vec2 vertexTextureCoordinates;
#define BASE_COLOR_TEXTURE_COORDINATES vertexTextureCoordinates
#else
#define BASE_COLOR_TEXTURE_COORDINATES vec2(0.0)
#endif

#include "mesh_vertex_common.glsl"

void main()
{
#ifdef HAS_NORMAL_MAP
    writeVertex(vertexPosition, vertexNormal, vec4(1.0), BASE_COLOR_TEXTURE_COORDINATES, vertexTangent);
#else
    writeVertex(vertexPosition, vertexNormal, vec4(1.0), BASE_COLOR_TEXTURE_COORDINATES);
#endif
}

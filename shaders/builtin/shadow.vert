#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 vertexPosition;
#ifdef HAS_BASE_COLOR_TEXTURE
layout(location = 2) in vec2 vertexTextureCoordinates;
#define BASE_COLOR_TEXTURE_COORDINATES vertexTextureCoordinates
#else
#define BASE_COLOR_TEXTURE_COORDINATES vec2(0.0)
#endif

#include "shadow_vertex_common.glsl"

void main()
{
    writeShadowVertex(vertexPosition, 1.0, BASE_COLOR_TEXTURE_COORDINATES);
}

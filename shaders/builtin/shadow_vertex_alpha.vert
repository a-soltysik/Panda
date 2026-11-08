#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderLocations.inc"

layout(location = PANDA_VERTEX_ATTRIBUTE_POSITION_LOCATION) in vec3 vertexPosition;
layout(location = PANDA_VERTEX_ATTRIBUTE_COLOR_LOCATION) in vec4 vertexColor;
#ifdef HAS_BASE_COLOR_TEXTURE
layout(location = PANDA_VERTEX_ATTRIBUTE_TEX_COORD_0_LOCATION) in vec2 vertexTextureCoordinates;
#define BASE_COLOR_TEXTURE_COORDINATES vertexTextureCoordinates
#else
#define BASE_COLOR_TEXTURE_COORDINATES vec2(0.0)
#endif

#include "shadow_vertex_common.glsl"

void main()
{
    writeShadowVertex(vertexPosition, vertexColor.a, BASE_COLOR_TEXTURE_COORDINATES);
}

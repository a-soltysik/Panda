#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vertexPosition;

#include "shadow_vertex_common.glsl"

void main()
{
    writeShadowVertex(vertexPosition, 1.0);
}

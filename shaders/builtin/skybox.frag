#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderInterface.inc"

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_SKYBOX_BINDING) uniform samplerCube skyboxMap;
layout(location = 0) in vec3 worldDirection;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(textureLod(skyboxMap, normalize(worldDirection), 0.0).rgb, 1.0);
}

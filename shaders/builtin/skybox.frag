#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderInterface.inc"

layout(constant_id = PANDA_MESH_OUTPUT_SRGB_SPECIALIZATION_ID) const uint outputIsSrgb = 1u;

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_SKYBOX_BINDING) uniform samplerCube skyboxMap;
layout(location = 0) in vec3 worldDirection;
layout(location = 0) out vec4 fragmentColor;

vec3 encodeSrgb(vec3 linearColor)
{
    const bvec3 lowRange = lessThanEqual(linearColor, vec3(0.0031308));
    const vec3 low = linearColor * 12.92;
    const vec3 high = (1.055 * pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.4))) - 0.055;
    return mix(high, low, lowRange);
}

vec3 toneMapAces(vec3 color)
{
    return clamp((color * ((2.51 * color) + 0.03)) / ((color * ((2.43 * color) + 0.59)) + 0.14), 0.0, 1.0);
}

void main()
{
    const vec3 mapped = toneMapAces(textureLod(skyboxMap, normalize(worldDirection), 0.0).rgb);
    const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
    fragmentColor = vec4(outputColor, 1.0);
}

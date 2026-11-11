#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderInterface.inc"

layout(constant_id = PANDA_SHADOW_ALPHA_MODE_SPECIALIZATION_ID) const uint alphaMode = 0u;

struct MaterialRecord
{
    float metallic;
    float roughness;
    uint isLit;
    uint renderFace;
    float alphaCutoff;
    uint hasBaseColorTexture;
    uint hasMetallicRoughnessTexture;
    uint hasEmissiveTexture;
    vec4 emissive;
    uint hasOcclusionTexture;
    float occlusionStrength;
    vec2 padding;
};

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_MATERIAL_DATA_BINDING, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

layout(location = PANDA_SHADOW_VARYING_ALPHA_LOCATION) in float vertexAlpha;
layout(location = PANDA_SHADOW_VARYING_MATERIAL_INDEX_LOCATION) flat in uint materialIndex;
#ifdef HAS_BASE_COLOR_TEXTURE
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_BASE_COLOR_BINDING) uniform sampler2D baseColorMap;
layout(location = PANDA_SHADOW_VARYING_TEXTURE_COORDINATES_LOCATION) in vec2 textureCoordinates;
#endif

void main()
{
    float alpha = vertexAlpha;
#ifdef HAS_BASE_COLOR_TEXTURE
    alpha *= texture(baseColorMap, textureCoordinates).a;
#endif
    if (alphaMode == 1u && alpha < materialData.records[materialIndex].alphaCutoff)
    {
        discard;
    }
}

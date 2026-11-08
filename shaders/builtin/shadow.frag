#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderLocations.inc"

layout(constant_id = 0) const uint alphaMode = 0u;

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
};

layout(set = 0, binding = 2, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

layout(location = PANDA_SHADOW_VARYING_ALPHA_LOCATION) in float vertexAlpha;
layout(location = PANDA_SHADOW_VARYING_MATERIAL_INDEX_LOCATION) flat in uint materialIndex;
#ifdef HAS_BASE_COLOR_TEXTURE
layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
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

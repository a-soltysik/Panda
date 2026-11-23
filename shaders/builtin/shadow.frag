#version 450
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
    vec4 emissiveAndNormalScale;
    uint hasOcclusionTexture;
    float occlusionStrength;
    vec2 padding;
};

layout(set = 0, binding = 2, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

layout(location = 0) in float vertexAlpha;
layout(location = 1) flat in uint materialIndex;
#ifdef HAS_BASE_COLOR_TEXTURE
layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
layout(location = 2) in vec2 textureCoordinates;
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

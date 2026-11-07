#version 450

layout(constant_id = 0) const uint alphaMode = 0u;

struct MaterialRecord
{
    float metallic;
    float roughness;
    uint isLit;
    uint renderFace;
    float alphaCutoff;
    uint padding[3];
};

layout(set = 0, binding = 2, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

layout(location = 0) in float vertexAlpha;
layout(location = 1) flat in uint materialIndex;

void main()
{
    if (alphaMode == 1u && vertexAlpha < materialData.records[materialIndex].alphaCutoff)
    {
        discard;
    }
}

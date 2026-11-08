#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderLocations.inc"

layout(constant_id = 0) const uint outputIsSrgb = 1u;
layout(constant_id = 1) const uint alphaMode = 0u;

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColorIntensity;
    vec4 ambient;
} frameData;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
struct MaterialRecord
{
    float metallic;
    float roughness;
    uint isLit;
    uint renderFace;
    float alphaCutoff;
    uint hasBaseColorTexture;
    uint padding[2];
};

layout(set = 0, binding = 2, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

layout(location = PANDA_MESH_VARYING_WORLD_POSITION_LOCATION) in vec3 worldPosition;
layout(location = PANDA_MESH_VARYING_WORLD_NORMAL_LOCATION) in vec3 worldNormal;
layout(location = PANDA_MESH_VARYING_BASE_COLOR_LOCATION) in vec4 baseColor;
layout(location = PANDA_MESH_VARYING_MATERIAL_INDEX_LOCATION) flat in uint materialIndex;
layout(location = PANDA_MESH_VARYING_TEXTURE_COORDINATES_LOCATION) in vec2 textureCoordinates;
layout(location = 0) out vec4 fragmentColor;

const float pi = 3.14159265359;
const vec3 dielectricF0 = vec3(0.04);

float distributionGgx(float normalHalf, float alphaSquared)
{
    const float denominator = (normalHalf * normalHalf * (alphaSquared - 1.0)) + 1.0;
    return alphaSquared / (pi * denominator * denominator);
}

float geometrySchlick(float normalDirection, float k)
{
    return normalDirection / ((normalDirection * (1.0 - k)) + k);
}

vec3 fresnelSchlick(float viewHalf, vec3 f0)
{
    return f0 + ((1.0 - f0) * pow(1.0 - viewHalf, 5.0));
}

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

float shadowVisibility(vec3 normal)
{
    const vec4 lightPosition = frameData.lightViewProjection * vec4(worldPosition + normal * 0.002, 1.0);
    if (abs(lightPosition.w) <= 0.000001)
    {
        return 1.0;
    }
    const vec3 shadowCoordinate = lightPosition.xyz / lightPosition.w;
    if (any(lessThan(shadowCoordinate.xy, vec2(-1.0))) || any(greaterThan(shadowCoordinate.xy, vec2(1.0))) ||
        shadowCoordinate.z < 0.0 || shadowCoordinate.z > 1.0)
    {
        return 1.0;
    }
    const vec2 shadowUv = shadowCoordinate.xy * 0.5 + 0.5;
    const ivec2 size = textureSize(shadowMap, 0);
    const ivec2 center = ivec2(shadowUv * vec2(size));
    const float lightFacing = max(dot(normal, normalize(-frameData.lightDirection.xyz)), 0.0);
    const float depthBias = max(0.0005, 0.002 * (1.0 - lightFacing));
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const ivec2 texel = clamp(center + ivec2(x, y), ivec2(0), size - 1);
            visible += shadowCoordinate.z - depthBias <= texelFetch(shadowMap, texel, 0).r ? 1.0 : 0.0;
        }
    }
    return visible / 9.0;
}

void main()
{
    const MaterialRecord material = materialData.records[materialIndex];
    const vec4 mappedBaseColor = baseColor *
        (material.hasBaseColorTexture != 0u ? texture(baseColorMap, textureCoordinates) : vec4(1.0));
    if (alphaMode == 1u && mappedBaseColor.a < material.alphaCutoff)
    {
        discard;
    }
    const float opacity = alphaMode == 2u ? clamp(mappedBaseColor.a, 0.0, 1.0) : mappedBaseColor.a;
    if (material.isLit == 0u)
    {
        const vec3 mapped = toneMapAces(mappedBaseColor.rgb);
        const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
        fragmentColor = vec4(outputColor, opacity);
        return;
    }
    vec3 normal = normalize(worldNormal);
    if (material.renderFace != 0u && !gl_FrontFacing)
    {
        normal = -normal;
    }
    const float metallic = material.metallic;
    const float roughness = clamp(material.roughness, 0.045, 1.0);
    const vec3 viewDirection = normalize(frameData.cameraPosition.xyz - worldPosition);
    const vec3 lightDirection = normalize(-frameData.lightDirection.xyz);
    const vec3 halfDirection = normalize(viewDirection + lightDirection);
    const float normalView = max(dot(normal, viewDirection), 0.001);
    const float normalLight = max(dot(normal, lightDirection), 0.0);
    const float normalHalf = max(dot(normal, halfDirection), 0.0);
    const float viewHalf = max(dot(viewDirection, halfDirection), 0.0);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float geometryK = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    const vec3 fresnel = fresnelSchlick(viewHalf, dielectricF0);
    const float distribution = distributionGgx(normalHalf, alphaSquared);
    const float geometry = geometrySchlick(normalView, geometryK) * geometrySchlick(normalLight, geometryK);
    const vec3 specular = (distribution * geometry * fresnel) / max(4.0 * normalView * normalLight, 0.001);
    const vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * mappedBaseColor.rgb / pi;
    const vec3 direct = (diffuse + specular) * frameData.lightColorIntensity.rgb *
    frameData.lightColorIntensity.w * normalLight * shadowVisibility(normal);
    const vec3 ambient = mappedBaseColor.rgb * frameData.ambient.rgb * frameData.ambient.w;
    const vec3 mapped = toneMapAces(ambient + direct);
    const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
    fragmentColor = vec4(outputColor, opacity);
}

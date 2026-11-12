#version 450
#extension GL_GOOGLE_include_directive : require

#include "ShaderInterface.inc"

layout(constant_id = PANDA_MESH_OUTPUT_SRGB_SPECIALIZATION_ID) const uint outputIsSrgb = 1u;
layout(constant_id = PANDA_MESH_ALPHA_MODE_SPECIALIZATION_ID) const uint alphaMode = 0u;

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_UNIFORM_BINDING) uniform FrameData
{
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 ambient;
    uvec4 lightInfo;
} frameData;

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_SHADOW_MAP_BINDING) uniform sampler2D shadowMap;
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_BASE_COLOR_BINDING) uniform sampler2D baseColorMap;
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_METALLIC_ROUGHNESS_BINDING) uniform sampler2D
    metallicRoughnessMap;
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_EMISSIVE_BINDING) uniform sampler2D emissiveMap;
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_OCCLUSION_BINDING) uniform sampler2D occlusionMap;
#ifdef HAS_NORMAL_MAP
layout(set = PANDA_TEXTURE_DESCRIPTOR_SET, binding = PANDA_TEXTURE_NORMAL_BINDING) uniform sampler2D normalMap;
#endif
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

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_MATERIAL_DATA_BINDING, std430) readonly buffer MaterialData
{
    MaterialRecord records[];
} materialData;

struct LightRecord
{
    vec4 positionRange;
    vec4 directionOuterConeCos;
    vec4 colorIntensity;
    float innerConeCos;
    uint type;
    uvec2 padding;
};

layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_LIGHT_DATA_BINDING, std430) readonly buffer LightData
{
    LightRecord records[];
} lightData;

layout(location = PANDA_MESH_VARYING_WORLD_POSITION_LOCATION) in vec3 worldPosition;
layout(location = PANDA_MESH_VARYING_WORLD_NORMAL_LOCATION) in vec3 worldNormal;
layout(location = PANDA_MESH_VARYING_BASE_COLOR_LOCATION) in vec4 baseColor;
layout(location = PANDA_MESH_VARYING_MATERIAL_INDEX_LOCATION) flat in uint materialIndex;
layout(location = PANDA_MESH_VARYING_TEXTURE_COORDINATES_LOCATION) in vec2 textureCoordinates;
#ifdef HAS_NORMAL_MAP
layout(location = PANDA_MESH_VARYING_WORLD_TANGENT_LOCATION) in vec4 worldTangent;
#endif
layout(location = PANDA_MESH_FRAGMENT_COLOR_LOCATION) out vec4 fragmentColor;

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

vec3 evaluateDirectLighting(vec3 baseColor,
                            vec3 normal,
                            vec3 geometricNormal,
                            float metallic,
                            float roughness)
{
    const vec3 viewDirection = normalize(frameData.cameraPosition.xyz - worldPosition);
    const float normalView = max(dot(normal, viewDirection), 0.001);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float geometryK = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    vec3 direct = vec3(0.0);
    for (uint lightIndex = 0u; lightIndex < frameData.lightInfo.x; ++lightIndex)
    {
        const LightRecord light = lightData.records[lightIndex];
        vec3 lightDirection;
        float attenuation = 1.0;
        if (light.type == uint(PANDA_LIGHT_TYPE_DIRECTIONAL))
        {
            lightDirection = normalize(-light.directionOuterConeCos.xyz);
        }
        else
        {
            const vec3 toLight = light.positionRange.xyz - worldPosition;
            const float distanceSquared = dot(toLight, toLight);
            if (distanceSquared <= 0.000000000001)
            {
                continue;
            }
            const float lightDistance = sqrt(distanceSquared);
            if (lightDistance >= light.positionRange.w)
            {
                continue;
            }
            lightDirection = toLight / lightDistance;
            const float distanceRatio = lightDistance / light.positionRange.w;
            attenuation = max(1.0 - pow(distanceRatio, 4.0), 0.0) / distanceSquared;
            if (light.type == uint(PANDA_LIGHT_TYPE_SPOT))
            {
                const float coneCosine = dot(-lightDirection, normalize(light.directionOuterConeCos.xyz));
                const float coneWidth = light.innerConeCos - light.directionOuterConeCos.w;
                const float coneAttenuation = clamp((coneCosine - light.directionOuterConeCos.w) / coneWidth, 0.0, 1.0);
                attenuation *= coneAttenuation * coneAttenuation;
            }
        }
        const vec3 halfDirection = normalize(viewDirection + lightDirection);
        const float normalLight = max(dot(normal, lightDirection), 0.0);
        const float normalHalf = max(dot(normal, halfDirection), 0.0);
        const float viewHalf = max(dot(viewDirection, halfDirection), 0.0);
        const vec3 fresnel = fresnelSchlick(viewHalf, mix(dielectricF0, baseColor, metallic));
        const float distribution = distributionGgx(normalHalf, alphaSquared);
        const float geometry = geometrySchlick(normalView, geometryK) * geometrySchlick(normalLight, geometryK);
        const vec3 specular = (distribution * geometry * fresnel) / max(4.0 * normalView * normalLight, 0.001);
        const vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor / pi;
        const float visibility = lightIndex == frameData.lightInfo.y ? shadowVisibility(geometricNormal) : 1.0;
        direct += (diffuse + specular) * light.colorIntensity.rgb * light.colorIntensity.w * normalLight *
            attenuation * visibility;
    }
    return direct;
}

void main()
{
    const MaterialRecord material = materialData.records[materialIndex];
    const vec3 emissive = material.emissiveAndNormalScale.rgb *
        (material.hasEmissiveTexture != 0u ? texture(emissiveMap, textureCoordinates).rgb : vec3(1.0));
    const vec4 mappedBaseColor = baseColor *
        (material.hasBaseColorTexture != 0u ? texture(baseColorMap, textureCoordinates) : vec4(1.0));
    if (alphaMode == 1u && mappedBaseColor.a < material.alphaCutoff)
    {
        discard;
    }
    const float opacity = alphaMode == 2u ? clamp(mappedBaseColor.a, 0.0, 1.0) : mappedBaseColor.a;
    if (material.isLit == 0u)
    {
        const vec3 mapped = toneMapAces(mappedBaseColor.rgb + emissive);
        const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
        fragmentColor = vec4(outputColor, opacity);
        return;
    }
    vec3 normal = normalize(worldNormal);
    if (material.renderFace != 0u && !gl_FrontFacing)
    {
        normal = -normal;
    }
    const vec3 geometricNormal = normal;
#ifdef HAS_NORMAL_MAP
    vec3 tangent = worldTangent.xyz - normal * dot(normal, worldTangent.xyz);
    const float tangentLengthSquared = dot(tangent, tangent);
    if (tangentLengthSquared > 0.00000001 && abs(worldTangent.w) > 0.5)
    {
        tangent *= inversesqrt(tangentLengthSquared);
        const vec3 bitangent = cross(normal, tangent) * worldTangent.w;
        vec3 mappedNormal = (texture(normalMap, textureCoordinates).xyz * 2.0) - 1.0;
        mappedNormal *= vec3(material.emissiveAndNormalScale.w, material.emissiveAndNormalScale.w, 1.0);
        const float largestComponent = max(max(abs(mappedNormal.x), abs(mappedNormal.y)), abs(mappedNormal.z));
        if (largestComponent > 0.0)
        {
            mappedNormal /= largestComponent;
            normal = normalize(mat3(tangent, bitangent, normal) * mappedNormal);
        }
    }
#endif
    const vec4 mappedMetallicRoughness =
        material.hasMetallicRoughnessTexture != 0u ? texture(metallicRoughnessMap, textureCoordinates) : vec4(1.0);
    const float metallic = clamp(material.metallic * mappedMetallicRoughness.b, 0.0, 1.0);
    const float roughness = clamp(material.roughness * mappedMetallicRoughness.g, 0.045, 1.0);
    const vec3 direct = evaluateDirectLighting(mappedBaseColor.rgb, normal, geometricNormal, metallic, roughness);
    const float occlusionSample =
        material.hasOcclusionTexture != 0u ? texture(occlusionMap, textureCoordinates).r : 1.0;
    const float ambientOcclusion = mix(1.0, occlusionSample, clamp(material.occlusionStrength, 0.0, 1.0));
    const vec3 ambient = mappedBaseColor.rgb * frameData.ambient.rgb * frameData.ambient.w * ambientOcclusion;
    const vec3 mapped = toneMapAces(ambient + direct + emissive);
    const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
    fragmentColor = vec4(outputColor, opacity);
}

#version 450

layout(constant_id = 0) const uint outputIsSrgb = 1u;

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColorIntensity;
    vec4 ambient;
} frameData;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec4 baseColor;
layout(location = 0) out vec4 fragmentColor;

const float pi = 3.14159265359;
const vec3 dielectricF0 = vec3(0.04);
const float metallic = 0.0;
const float roughness = 1.0;

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

void main()
{
    const vec3 normal = normalize(worldNormal);
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
    const vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor.rgb / pi;
    const vec3 direct = (diffuse + specular) * frameData.lightColorIntensity.rgb *
                        frameData.lightColorIntensity.w * normalLight;
    const vec3 ambient = baseColor.rgb * frameData.ambient.rgb * frameData.ambient.w;
    const vec3 mapped = toneMapAces(ambient + direct);
    const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
    fragmentColor = vec4(outputColor, baseColor.a);
}

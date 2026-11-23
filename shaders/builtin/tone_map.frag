#version 450
layout(constant_id = 0) const uint outputIsSrgb = 1u;
layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(location = 0) in vec2 textureCoordinates;
layout(location = 0) out vec4 fragmentColor;

vec3 toneMapAces(vec3 color)
{
    return clamp((color * ((2.51 * color) + 0.03)) / ((color * ((2.43 * color) + 0.59)) + 0.14), 0.0, 1.0);
}

vec3 encodeSrgb(vec3 linearColor)
{
    const bvec3 lowRange = lessThanEqual(linearColor, vec3(0.0031308));
    const vec3 low = linearColor * 12.92;
    const vec3 high = (1.055 * pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.4))) - 0.055;
    return mix(high, low, lowRange);
}

void main()
{
    const vec3 mapped = toneMapAces(texture(hdrColor, textureCoordinates).rgb);
    const vec3 outputColor = outputIsSrgb == 1u ? mapped : encodeSrgb(mapped);
    fragmentColor = vec4(outputColor, 1.0);
}

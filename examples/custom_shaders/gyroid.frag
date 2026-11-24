#version 450

layout(location = 0) in vec3 localPosition;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 7, std140) uniform GyroidParameters
{
    float elapsedSeconds;
} params;

void main()
{
    const float sine = sin(params.elapsedSeconds);
    const float cosine = cos(params.elapsedSeconds);
    const vec2 rotated = mat2(cosine, sine, -sine, cosine) * localPosition.xz;
    const vec3 position = vec3(rotated.x, localPosition.y, rotated.y) * 9.0 + vec3(0.6, -0.25, 0.9);
    const float field = dot(sin(position), cos(position.yzx));
    const float distanceToContour = abs(field);
    const float contour = 1.0 - smoothstep(0.04, 0.12 + fwidth(field), distanceToContour);
    const float halo = exp(-4.0 * distanceToContour);
    const float hue = 0.5 + 0.5 * sin(dot(position, vec3(0.27, 0.43, -0.34)));
    const vec3 ink = mix(vec3(0.025, 0.85, 0.68), vec3(0.72, 0.08, 0.92), hue);
    color = vec4(vec3(0.014, 0.023, 0.056) + ink * (0.23 * halo + 0.9 * contour), 1.0);
}

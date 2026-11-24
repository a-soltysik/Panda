#version 450

layout(set = 0, binding = 5) uniform ShadowViewData
{
    mat4 lightViewProjection;
} shadowViewData;

layout(set = 2, binding = 3, std430) readonly buffer InstanceColors
{
    vec4 values[];
} instanceColors;

void main()
{
    const vec2 positions[3] = vec2[3](vec2(-0.3, -0.3), vec2(0.3, -0.3), vec2(0.0, 0.3));
    const float center = -0.5 + float(gl_InstanceIndex);
    vec3 position = vec3(positions[gl_VertexIndex] + vec2(center, 0.0), 0.5);
    position.x += instanceColors.values[gl_InstanceIndex].r * 0.01;
    gl_Position = shadowViewData.lightViewProjection * vec4(position, 1.0);
}

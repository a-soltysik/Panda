#version 450

layout(location = 3) in vec3 vertexPosition;

layout(set = 0, binding = 5) uniform ShadowViewData
{
    mat4 lightViewProjection;
} shadowViewData;

layout(set = 2, binding = 3, std430) readonly buffer ShadowValues
{
    vec4 values;
} shadowValues;

void main()
{
    const vec3 position = vertexPosition + vec3(shadowValues.values.x, 0.0, 0.0);
    gl_Position = shadowViewData.lightViewProjection * vec4(position, 1.0);
}

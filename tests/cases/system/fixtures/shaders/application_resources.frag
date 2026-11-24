#version 450

layout(location = 0) out vec4 color;

layout(set = 2, binding = 7, std140) uniform Parameters
{
    vec4 values;
} params;

layout(set = 2, binding = 3, std430) readonly buffer FirstValues
{
    vec4 values;
} firstValues;

layout(set = 3, binding = 9, std430) readonly buffer SecondValues
{
    vec4 values;
} secondValues;

void main()
{
    color = vec4(params.values.x, firstValues.values.x, secondValues.values.x, 1.0);
}

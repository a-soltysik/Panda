#version 450

layout(location = 4) in vec3 vertexPosition;

layout(set = 0, binding = 5) uniform ShadowViewData
{
    mat4 lightViewProjection;
} shadowViewData;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;

void main()
{
    gl_Position = shadowViewData.lightViewProjection * drawData.modelMatrix * vec4(vertexPosition, 1.0);
}

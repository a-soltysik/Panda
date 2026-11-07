#version 450

layout(location = 0) in vec3 vertexPosition;

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
    mat4 lightViewProjection;
} frameData;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;

void main()
{
    gl_Position = frameData.lightViewProjection * drawData.modelMatrix * vec4(vertexPosition, 1.0);
}

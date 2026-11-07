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

layout(location = 0) out float vertexAlpha;
layout(location = 1) flat out uint materialIndex;

void writeShadowVertex(vec3 position, float alpha)
{
    gl_Position = frameData.lightViewProjection * drawData.modelMatrix * vec4(position, 1.0);
    vertexAlpha = alpha * drawData.tint.a;
    materialIndex = uint(gl_InstanceIndex);
}

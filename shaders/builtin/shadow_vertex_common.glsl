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

layout(location = PANDA_SHADOW_VARYING_ALPHA_LOCATION) out float vertexAlpha;
layout(location = PANDA_SHADOW_VARYING_MATERIAL_INDEX_LOCATION) flat out uint materialIndex;
layout(location = PANDA_SHADOW_VARYING_TEXTURE_COORDINATES_LOCATION) out vec2 textureCoordinates;

void writeShadowVertex(vec3 position, float alpha, vec2 vertexTextureCoordinates)
{
    gl_Position = frameData.lightViewProjection * drawData.modelMatrix * vec4(position, 1.0);
    vertexAlpha = alpha * drawData.tint.a;
    materialIndex = uint(gl_InstanceIndex);
    textureCoordinates = vertexTextureCoordinates;
}

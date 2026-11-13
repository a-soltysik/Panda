layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_SHADOW_VIEW_BINDING) uniform ShadowViewData
{
    mat4 lightViewProjection;
} shadowViewData;

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
    gl_Position = shadowViewData.lightViewProjection * drawData.modelMatrix * vec4(position, 1.0);
    vertexAlpha = alpha * drawData.tint.a;
    materialIndex = uint(gl_InstanceIndex);
    textureCoordinates = vertexTextureCoordinates;
}

layout(set = 0, binding = 5) uniform ShadowViewData
{
    mat4 lightViewProjection;
} shadowViewData;

#ifdef PANDA_INSTANCED
layout(location = 5) in mat4 instanceModelMatrix;
layout(location = 12) in vec4 instanceTint;
layout(location = 13) in uint instanceMaterialIndex;
#else
layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;
#endif

layout(location = 0) out float vertexAlpha;
layout(location = 1) flat out uint materialIndex;
layout(location = 2) out vec2 textureCoordinates;

void writeShadowVertex(vec3 position, float alpha, vec2 vertexTextureCoordinates)
{
#ifdef PANDA_INSTANCED
    gl_Position = shadowViewData.lightViewProjection * instanceModelMatrix * vec4(position, 1.0);
    vertexAlpha = alpha * instanceTint.a;
    materialIndex = instanceMaterialIndex;
#else
    gl_Position = shadowViewData.lightViewProjection * drawData.modelMatrix * vec4(position, 1.0);
    vertexAlpha = alpha * drawData.tint.a;
    materialIndex = uint(gl_InstanceIndex);
#endif
    textureCoordinates = vertexTextureCoordinates;
}

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
} frameData;

#ifdef PANDA_INSTANCED
layout(location = 5) in mat4 instanceModelMatrix;
layout(location = 9) in mat3x4 instanceNormalColumns;
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

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 baseColor;
layout(location = 3) flat out uint materialIndex;
layout(location = 4) out vec2 textureCoordinates;
#ifdef HAS_NORMAL_MAP
layout(location = 5) out vec4 worldTangent;
#endif

void writeVertex(vec3 vertexPosition, vec3 vertexNormal, vec4 vertexColor, vec2 vertexTextureCoordinates)
{
#ifdef PANDA_INSTANCED
    const mat4 modelMatrix = instanceModelMatrix;
    const mat3 normalMatrix = mat3(instanceNormalColumns);
    const vec4 tint = instanceTint;
    const uint drawMaterialIndex = instanceMaterialIndex;
#else
    const mat4 modelMatrix = drawData.modelMatrix;
    const mat3 normalMatrix = mat3(drawData.normalColumns);
    const vec4 tint = drawData.tint;
    const uint drawMaterialIndex = uint(gl_InstanceIndex);
#endif
    const vec4 position = modelMatrix * vec4(vertexPosition, 1.0);
    gl_Position = frameData.viewProjection * position;
    worldPosition = position.xyz;
    worldNormal = normalize(normalMatrix * vertexNormal);
    baseColor = vertexColor * tint;
    materialIndex = drawMaterialIndex;
    textureCoordinates = vertexTextureCoordinates;
}

#ifdef HAS_NORMAL_MAP
void writeVertex(vec3 vertexPosition,
                 vec3 vertexNormal,
                 vec4 vertexColor,
                 vec2 vertexTextureCoordinates,
                 vec4 vertexTangent)
{
    writeVertex(vertexPosition, vertexNormal, vertexColor, vertexTextureCoordinates);
#ifdef PANDA_INSTANCED
    const float orientation = sign(determinant(mat3(instanceModelMatrix)));
    const mat4 modelMatrix = instanceModelMatrix;
#else
    const float orientation = sign(determinant(mat3(drawData.modelMatrix)));
    const mat4 modelMatrix = drawData.modelMatrix;
#endif
    worldTangent = vec4(mat3(modelMatrix) * vertexTangent.xyz, vertexTangent.w * orientation);
}
#endif

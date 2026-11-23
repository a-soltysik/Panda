layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
} frameData;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;

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
    const vec4 position = drawData.modelMatrix * vec4(vertexPosition, 1.0);
    gl_Position = frameData.viewProjection * position;
    worldPosition = position.xyz;
    worldNormal = normalize(mat3(drawData.normalColumns) * vertexNormal);
    baseColor = vertexColor * drawData.tint;
    materialIndex = uint(gl_InstanceIndex);
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
    const float orientation = sign(determinant(mat3(drawData.modelMatrix)));
    worldTangent = vec4(mat3(drawData.modelMatrix) * vertexTangent.xyz, vertexTangent.w * orientation);
}
#endif

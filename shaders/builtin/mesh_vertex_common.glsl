layout(set = PANDA_FRAME_DESCRIPTOR_SET, binding = PANDA_FRAME_UNIFORM_BINDING) uniform FrameData
{
    mat4 viewProjection;
} frameData;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;

layout(location = PANDA_MESH_VARYING_WORLD_POSITION_LOCATION) out vec3 worldPosition;
layout(location = PANDA_MESH_VARYING_WORLD_NORMAL_LOCATION) out vec3 worldNormal;
layout(location = PANDA_MESH_VARYING_BASE_COLOR_LOCATION) out vec4 baseColor;
layout(location = PANDA_MESH_VARYING_MATERIAL_INDEX_LOCATION) flat out uint materialIndex;
layout(location = PANDA_MESH_VARYING_TEXTURE_COORDINATES_LOCATION) out vec2 textureCoordinates;
#ifdef HAS_NORMAL_MAP
layout(location = PANDA_MESH_VARYING_WORLD_TANGENT_LOCATION) out vec4 worldTangent;
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

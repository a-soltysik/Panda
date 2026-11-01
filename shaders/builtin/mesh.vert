#version 450

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 4) in vec4 vertexColor;

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColorIntensity;
    vec4 ambient;
} frameData;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    vec4 normalColumn0;
    vec4 normalColumn1;
    vec4 normalColumn2;
    vec4 tint;
} drawData;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 baseColor;

void main()
{
    const vec4 position = drawData.modelMatrix * vec4(vertexPosition, 1.0);
    gl_Position = frameData.viewProjection * position;
    worldPosition = position.xyz;
    worldNormal = normalize(mat3(drawData.normalColumn0.xyz, drawData.normalColumn1.xyz, drawData.normalColumn2.xyz) *
                            vertexNormal);
    baseColor = vertexColor * drawData.tint;
}

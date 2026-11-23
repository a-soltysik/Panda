#version 450
layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    uvec4 lightInfo;
} frameData;

layout(location = 0) out vec3 worldDirection;

void main()
{
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 clipPosition = positions[gl_VertexIndex];
    const mat4 inverseViewProjection = inverse(frameData.viewProjection);
    const vec4 nearWorld = inverseViewProjection * vec4(clipPosition, 0.0, 1.0);
    const vec4 farWorld = inverseViewProjection * vec4(clipPosition, 1.0, 1.0);
    worldDirection = (farWorld.xyz * nearWorld.w) - (nearWorld.xyz * farWorld.w);
    gl_Position = vec4(clipPosition, 1.0, 1.0);
}

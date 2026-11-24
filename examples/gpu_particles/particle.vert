#version 450

struct ParticleInstance
{
    vec4 position;
    vec4 velocity;
};

layout(set = 2, binding = 1, std430) readonly buffer ParticleInstances
{
    ParticleInstance values[];
} particleInstances;

layout(set = 0, binding = 0) uniform FrameData
{
    mat4 viewProjection;
} frameData;

layout(set = 2, binding = 0, std140) uniform ParticleParameters
{
    vec4 maximumSpeedAndAspectRatio;
} particleParameters;

layout(push_constant) uniform DrawData
{
    mat4 modelMatrix;
    mat3x4 normalColumns;
    vec4 tint;
} drawData;

layout(location = 0) noperspective out vec2 particleCoordinate;
layout(location = 1) flat out uint particleIndex;

void main()
{
    const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const ParticleInstance particle = particleInstances.values[gl_InstanceIndex];
    const vec4 clipCenter = frameData.viewProjection * drawData.modelMatrix * particle.position;
    const vec2 corner = corners[gl_VertexIndex % 6];
    particleCoordinate = corner;
    particleIndex = uint(gl_InstanceIndex);
    const float spriteRadius = 0.006;
    const vec2 aspectCorrectedCorner = corner * vec2(1.0 / particleParameters.maximumSpeedAndAspectRatio.y, 1.0);
    gl_Position = vec4((clipCenter.xy / clipCenter.w + aspectCorrectedCorner * spriteRadius) * clipCenter.w,
                       clipCenter.z,
                       clipCenter.w);
}

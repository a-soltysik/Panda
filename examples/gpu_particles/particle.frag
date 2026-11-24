#version 450

struct ParticleInstance
{
    vec4 position;
    vec4 velocity;
};

layout(set = 2, binding = 0, std140) uniform ParticleParameters
{
    vec4 maximumSpeedAndAspectRatio;
} particleParameters;

layout(set = 2, binding = 1, std430) readonly buffer ParticleInstances
{
    ParticleInstance values[];
} particleInstances;

layout(location = 0) noperspective in vec2 particleCoordinate;
layout(location = 1) flat in uint particleIndex;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    if (dot(particleCoordinate, particleCoordinate) > 1.0)
    {
        discard;
    }
    const float speed = length(particleInstances.values[particleIndex].velocity.xyz);
    const vec3 slowColor = vec3(0.025, 0.12, 0.95);
    const vec3 fastColor = vec3(1.0, 0.19, 0.025);
    const float speedRatio = clamp(speed / particleParameters.maximumSpeedAndAspectRatio.x, 0.0, 1.0);
    fragmentColor = vec4(mix(slowColor, fastColor, speedRatio), 1.0);
}

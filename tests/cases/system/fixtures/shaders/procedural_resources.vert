#version 450

layout(location = 0) flat out uint instanceIndex;

void main()
{
    const vec2 positions[3] = vec2[3](vec2(-0.3, -0.3), vec2(0.3, -0.3), vec2(0.0, 0.3));
    const float center = -0.5 + float(gl_InstanceIndex);
    gl_Position = vec4(positions[gl_VertexIndex] + vec2(center, 0.0), 0.5, 1.0);
    instanceIndex = uint(gl_InstanceIndex);
}

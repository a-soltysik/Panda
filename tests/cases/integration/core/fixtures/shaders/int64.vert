#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

void main()
{
    int64_t position = int64_t(gl_VertexIndex) + 4294967296L;
    gl_Position = vec4(float(position), 0.0, 0.0, 1.0);
}

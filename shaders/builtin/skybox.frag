#version 450
layout(set = 0, binding = 8) uniform samplerCube skyboxMap;
layout(location = 0) in vec3 worldDirection;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(textureLod(skyboxMap, normalize(worldDirection), 0.0).rgb, 1.0);
}

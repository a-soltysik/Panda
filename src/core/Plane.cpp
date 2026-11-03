#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <panda/Geometry.hpp>
#include <panda/Plane.hpp>
#include <vector>

namespace panda
{
auto createPlaneGeometry() -> Geometry
{
    static constexpr auto positions = std::array {
        glm::vec3 {-0.5F, 0.0F, -0.5F},
        glm::vec3 {0.5F,  0.0F, -0.5F},
        glm::vec3 {0.5F,  0.0F, 0.5F },
        glm::vec3 {-0.5F, 0.0F, 0.5F }
    };
    static constexpr auto textureCoordinates = std::array {
        glm::vec2 {0.0F, 0.0F},
        glm::vec2 {1.0F, 0.0F},
        glm::vec2 {1.0F, 1.0F},
        glm::vec2 {0.0F, 1.0F}
    };

    auto geometry = Geometry {};
    geometry.vertices.reserve(positions.size());
    for (auto index = std::size_t {}; index < positions.size(); ++index)
    {
        geometry.vertices.push_back(MeshVertex {
            .position = positions[index],
            .normal = {0.0F, 1.0F, 0.0F},
            .uv = textureCoordinates[index],
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F}
        });
    }
    geometry.indices = std::vector<std::uint16_t> {0, 2, 1, 0, 3, 2};
    return geometry;
}
}

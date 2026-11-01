#include <array>
#include <cstddef>
#include <cstdint>
#include <panda/Cube.hpp>
#include <panda/Geometry.hpp>
#include <vector>

namespace
{
struct Face
{
    std::array<float, 3> normal;
    std::array<float, 3> tangent;
    std::array<float, 3> bitangent;
};

constexpr auto faces = std::array {
    Face {.normal = {1, 0, 0},  .tangent = {0, 0, -1}, .bitangent = {0, 1, 0} },
    Face {.normal = {-1, 0, 0}, .tangent = {0, 0, 1},  .bitangent = {0, 1, 0} },
    Face {.normal = {0, 1, 0},  .tangent = {1, 0, 0},  .bitangent = {0, 0, -1}},
    Face {.normal = {0, -1, 0}, .tangent = {1, 0, 0},  .bitangent = {0, 0, 1} },
    Face {.normal = {0, 0, 1},  .tangent = {1, 0, 0},  .bitangent = {0, 1, 0} },
    Face {.normal = {0, 0, -1}, .tangent = {-1, 0, 0}, .bitangent = {0, 1, 0} },
};
constexpr auto coordinates = std::array {
    std::array {0.0F, 0.0F},
     std::array {1.0F, 0.0F},
     std::array {1.0F, 1.0F},
     std::array {0.0F, 1.0F}
};
constexpr auto faceIndices = std::array {
    std::uint16_t {0}, std::uint16_t {1}, std::uint16_t {2}, std::uint16_t {0}, std::uint16_t {2}, std::uint16_t {3}};

auto createVertex(const Face& face, const std::array<float, 2>& textureCoordinates) -> panda::MeshVertex
{
    auto vertex = panda::MeshVertex {};
    vertex.normal = face.normal;
    vertex.uv = textureCoordinates;
    vertex.tangent = {face.tangent[0], face.tangent[1], face.tangent[2], 1.0F};
    for (auto axis = std::size_t {0}; axis < vertex.position.size(); ++axis)
    {
        vertex.position[axis] = (0.5F * face.normal[axis]) + ((textureCoordinates[0] - 0.5F) * face.tangent[axis]) +
                                ((textureCoordinates[1] - 0.5F) * face.bitangent[axis]);
    }
    return vertex;
}
}

namespace panda
{
auto createCubeGeometry() -> Geometry
{
    auto result = Geometry {};
    result.vertices.resize(faces.size() * coordinates.size());
    auto& indices = std::get<std::vector<std::uint16_t>>(result.indices);
    indices.resize(faces.size() * faceIndices.size());
    for (auto faceIndex = std::size_t {0}; faceIndex < faces.size(); ++faceIndex)
    {
        const auto& face = faces[faceIndex];
        for (auto corner = std::size_t {0}; corner < coordinates.size(); ++corner)
        {
            result.vertices[(faceIndex * 4) + corner] = createVertex(face, coordinates[corner]);
        }
        for (auto index = std::size_t {0}; index < faceIndices.size(); ++index)
        {
            indices[(faceIndex * 6) + index] = static_cast<std::uint16_t>((faceIndex * 4) + faceIndices[index]);
        }
    }
    return result;
}
}

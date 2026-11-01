#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <glm/geometric.hpp>
#include <panda/Cube.hpp>
#include <panda/Geometry.hpp>
#include <vector>

TEST(CubeGeometry, HasStandardLayoutAndOutwardTriangles)
{
    EXPECT_EQ(sizeof(panda::MeshVertex), 64);
    EXPECT_EQ(offsetof(panda::MeshVertex, normal), 12);
    EXPECT_EQ(offsetof(panda::MeshVertex, uv), 24);
    EXPECT_EQ(offsetof(panda::MeshVertex, tangent), 32);
    EXPECT_EQ(offsetof(panda::MeshVertex, color), 48);
    const auto cube = panda::createCubeGeometry();
    const auto& indices = std::get<std::vector<std::uint16_t>>(cube.indices);
    EXPECT_EQ(cube.vertices.size(), 24);
    EXPECT_EQ(indices.size(), 36);
    for (auto triangle = std::size_t {0}; triangle < indices.size(); triangle += 3)
    {
        const auto& firstVertex = cube.vertices.at(indices.at(triangle));
        const auto& secondVertex = cube.vertices.at(indices.at(triangle + 1));
        const auto& thirdVertex = cube.vertices.at(indices.at(triangle + 2));
        const auto firstEdge = secondVertex.position - firstVertex.position;
        const auto secondEdge = thirdVertex.position - firstVertex.position;
        const auto orientation = glm::dot(glm::cross(firstEdge, secondEdge), firstVertex.normal);
        EXPECT_FLOAT_EQ(orientation, 1.0F);
        EXPECT_FLOAT_EQ(firstVertex.position.x * firstVertex.position.x, 0.25F);
        EXPECT_FLOAT_EQ(firstVertex.position.y * firstVertex.position.y, 0.25F);
        EXPECT_FLOAT_EQ(firstVertex.position.z * firstVertex.position.z, 0.25F);
    }
}

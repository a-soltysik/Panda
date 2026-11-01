#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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
        const auto firstX = secondVertex.position[0] - firstVertex.position[0];
        const auto firstY = secondVertex.position[1] - firstVertex.position[1];
        const auto firstZ = secondVertex.position[2] - firstVertex.position[2];
        const auto secondX = thirdVertex.position[0] - firstVertex.position[0];
        const auto secondY = thirdVertex.position[1] - firstVertex.position[1];
        const auto secondZ = thirdVertex.position[2] - firstVertex.position[2];
        const auto orientation = (((firstY * secondZ) - (firstZ * secondY)) * firstVertex.normal[0]) +
                                 (((firstZ * secondX) - (firstX * secondZ)) * firstVertex.normal[1]) +
                                 (((firstX * secondY) - (firstY * secondX)) * firstVertex.normal[2]);
        EXPECT_FLOAT_EQ(orientation, 1.0F);
        for (const auto value : firstVertex.position)
        {
            EXPECT_FLOAT_EQ(value * value, 0.25F);
        }
    }
}

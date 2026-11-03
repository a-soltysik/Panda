#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/geometric.hpp>
#include <panda/Plane.hpp>
#include <vector>

TEST(PlaneGeometry, HasUpwardFacingUnitSquare)
{
    const auto plane = panda::createPlaneGeometry();
    const auto& indices = std::get<std::vector<std::uint16_t>>(plane.indices);
    ASSERT_EQ(plane.vertices.size(), 4);
    ASSERT_EQ(indices.size(), 6);
    for (const auto& vertex : plane.vertices)
    {
        EXPECT_FLOAT_EQ(vertex.normal.y, 1.0F);
        EXPECT_FLOAT_EQ(vertex.normal.x, 0.0F);
        EXPECT_FLOAT_EQ(vertex.normal.z, 0.0F);
        EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 0.5F);
        EXPECT_FLOAT_EQ(vertex.position.y, 0.0F);
        EXPECT_FLOAT_EQ(std::abs(vertex.position.z), 0.5F);
    }
    for (auto triangle = std::size_t {}; triangle < indices.size(); triangle += 3)
    {
        const auto& first = plane.vertices.at(indices.at(triangle));
        const auto& second = plane.vertices.at(indices.at(triangle + 1));
        const auto& third = plane.vertices.at(indices.at(triangle + 2));
        const auto orientation = glm::cross(second.position - first.position, third.position - first.position);
        EXPECT_GT(orientation.y, 0.0F);
    }
}

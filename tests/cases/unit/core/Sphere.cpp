#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <glm/geometric.hpp>
#include <panda/Sphere.hpp>
#include <variant>
#include <vector>

TEST(SphereGeometry, HasDefaultOutwardTessellationAndUnitNormals)
{
    const auto sphere = panda::createSphereGeometry();
    ASSERT_TRUE(sphere);
    const auto& indices = std::get<std::vector<std::uint16_t>>(sphere->indices);
    ASSERT_EQ(sphere->vertices.size(), 561);
    ASSERT_EQ(indices.size(), 2880);
    for (const auto& vertex : sphere->vertices)
    {
        EXPECT_NEAR(glm::length(vertex.position), 0.5F, 1.0e-6F);
        EXPECT_NEAR(glm::length(vertex.normal), 1.0F, 1.0e-6F);
        EXPECT_NEAR(glm::dot(vertex.normal, glm::vec3 {vertex.tangent}), 0.0F, 1.0e-6F);
        EXPECT_GE(vertex.uv.x, 0.0F);
        EXPECT_LE(vertex.uv.x, 1.0F);
        EXPECT_GE(vertex.uv.y, 0.0F);
        EXPECT_LE(vertex.uv.y, 1.0F);
    }
    for (auto triangle = std::size_t {}; triangle < indices.size(); triangle += 3)
    {
        const auto& first = sphere->vertices.at(indices.at(triangle));
        const auto& second = sphere->vertices.at(indices.at(triangle + 1));
        const auto& third = sphere->vertices.at(indices.at(triangle + 2));
        const auto faceNormal = glm::cross(second.position - first.position, third.position - first.position);
        EXPECT_GT(glm::dot(faceNormal, first.normal + second.normal + third.normal), 0.0F);
    }
}

TEST(SphereGeometry, UsesRequestedResolutionAndRejectsInvalidOrUnrepresentableCounts)
{
    const auto sphere = panda::createSphereGeometry({.longitudeSegments = 8, .latitudeRings = 4});
    ASSERT_TRUE(sphere);
    EXPECT_EQ(sphere->vertices.size(), 45);
    EXPECT_EQ(std::get<std::vector<std::uint16_t>>(sphere->indices).size(), 144);
    EXPECT_FALSE(panda::createSphereGeometry({.longitudeSegments = 2, .latitudeRings = 4}));
    EXPECT_FALSE(panda::createSphereGeometry({.longitudeSegments = 8, .latitudeRings = 1}));
    EXPECT_FALSE(panda::createSphereGeometry({.longitudeSegments = 100000, .latitudeRings = 100000}));
    EXPECT_FALSE(panda::createSphereGeometry({.longitudeSegments = 49000000, .latitudeRings = 86}));
    const auto largeSphere = panda::createSphereGeometry({.longitudeSegments = 256, .latitudeRings = 256});
    ASSERT_TRUE(largeSphere);
    EXPECT_TRUE(std::holds_alternative<std::vector<std::uint32_t>>(largeSphere->indices));
}

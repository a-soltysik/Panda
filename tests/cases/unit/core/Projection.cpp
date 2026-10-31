#include <gtest/gtest.h>

#include <glm/ext/vector_float4.hpp>
#include <limits>
#include <numbers>
#include <panda/Projection.hpp>

TEST(Projection, PerspectiveMapsDepthAndFlipsY)
{
    const auto projection = panda::createPerspectiveProjection(std::numbers::pi_v<float> / 2.0F, 2.0F, 1.0F, 10.0F);
    ASSERT_TRUE(projection);
    const auto nearPoint = *projection * glm::vec4 {0.0F, 0.0F, -1.0F, 1.0F};
    const auto farPoint = *projection * glm::vec4 {0.0F, 0.0F, -10.0F, 1.0F};
    const auto upperRight = *projection * glm::vec4 {2.0F, 1.0F, -1.0F, 1.0F};
    EXPECT_NEAR(nearPoint.z / nearPoint.w, 0.0F, 1e-6F);
    EXPECT_NEAR(farPoint.z / farPoint.w, 1.0F, 1e-6F);
    EXPECT_NEAR(upperRight.x / upperRight.w, 1.0F, 1e-6F);
    EXPECT_NEAR(upperRight.y / upperRight.w, -1.0F, 1e-6F);
}

TEST(Projection, OrthographicMapsDepthAndFlipsY)
{
    const auto projection = panda::createOrthographicProjection(2.0F, 2.0F, 1.0F, 10.0F);
    ASSERT_TRUE(projection);
    const auto nearCorner = *projection * glm::vec4 {2.0F, 1.0F, -1.0F, 1.0F};
    const auto farPoint = *projection * glm::vec4 {0.0F, 0.0F, -10.0F, 1.0F};
    EXPECT_NEAR(nearCorner.x, 1.0F, 1e-6F);
    EXPECT_NEAR(nearCorner.y, -1.0F, 1e-6F);
    EXPECT_NEAR(nearCorner.z, 0.0F, 1e-6F);
    EXPECT_NEAR(farPoint.z, 1.0F, 1e-6F);
}

TEST(Projection, RejectsInvalidOrUnrepresentableParameters)
{
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(panda::createPerspectiveProjection(0.0F, 1.0F, 1.0F, 10.0F));
    EXPECT_FALSE(panda::createPerspectiveProjection(std::numbers::pi_v<float>, 1.0F, 1.0F, 10.0F));
    EXPECT_FALSE(panda::createPerspectiveProjection(1.0F, nan, 1.0F, 10.0F));
    EXPECT_FALSE(panda::createPerspectiveProjection(1.0F, 1.0F, 1.0F, infinity));
    EXPECT_FALSE(panda::createOrthographicProjection(-1.0F, 1.0F, 1.0F, 10.0F));
    EXPECT_FALSE(panda::createOrthographicProjection(1.0F, 0.0F, 1.0F, 10.0F));
    EXPECT_FALSE(panda::createOrthographicProjection(1.0F, 1.0F, 1.0F, 1.0F));
    EXPECT_FALSE(panda::createOrthographicProjection(std::numeric_limits<float>::denorm_min(), 1.0F, 1.0F, 10.0F));
}

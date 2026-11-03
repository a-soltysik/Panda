#include <gtest/gtest.h>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <panda/Transform.hpp>

TEST(Transform, DefaultsToIdentity)
{
    const auto matrix = panda::createModelMatrix({});
    ASSERT_TRUE(matrix);
    EXPECT_EQ(*matrix, glm::mat4 {1.0F});
}

TEST(Transform, ComposesTranslationRotationAndScale)
{
    const auto matrix = panda::createModelMatrix(panda::Transform {
        .position = {3.0F, 4.0F, 5.0F},
        .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 0.0F, 1.0F}
          ),
        .scale = {2.0F, 3.0F, 4.0F}
    });
    ASSERT_TRUE(matrix);
    const auto point = *matrix * glm::vec4 {1.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_NEAR(point.x, 3.0F, 1e-6F);
    EXPECT_NEAR(point.y, 6.0F, 1e-6F);
    EXPECT_NEAR(point.z, 5.0F, 1e-6F);
}

TEST(Transform, NormalizesFiniteNonUnitRotation)
{
    auto rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0, 0, 1});
    rotation *= 2.0F;
    const auto matrix = panda::createModelMatrix(panda::Transform {.rotation = rotation});
    ASSERT_TRUE(matrix);
    const auto point = *matrix * glm::vec4 {1.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_NEAR(point.x, 0.0F, 1e-6F);
    EXPECT_NEAR(point.y, 1.0F, 1e-6F);
}

TEST(Transform, RejectsNonFiniteAndSingularValues)
{
    static constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .position = {nan, 0.0F, 0.0F}
    }));
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .rotation = glm::quat {nan, 0.0F, 0.0F, 1.0F}
    }));
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .scale = {1.0F, std::numeric_limits<float>::infinity(), 1.0F}
    }));
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .rotation = glm::quat {0.0F, 0.0F, 0.0F, 0.0F}
    }));
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .scale = {1.0F, 0.0F, 1.0F}
    }));
    EXPECT_FALSE(panda::createModelMatrix(panda::Transform {
        .scale = {1.0e-7F, 1.0F, 1.0F}
    }));
}

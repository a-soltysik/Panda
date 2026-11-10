#include "SceneTransformMath.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <panda/Error.hpp>
#include <panda/Transform.hpp>

namespace
{
void expectMatrixNear(const glm::mat4& actual, const glm::mat4& expected)
{
    for (auto column = 0; column < 4; ++column)
    {
        for (auto row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(actual[column][row], expected[column][row], 1.0e-5F);
        }
    }
}

void expectRotationNear(const glm::quat& actual, const glm::quat& expected)
{
    EXPECT_NEAR(std::abs(glm::dot(actual, expected)), 1.0F, 1.0e-5F);
}
}

TEST(SceneTransformMath, ValidateTransformNormalizesLargeFiniteRotation)
{
    const panda::Transform transform {
        .position = {1.0F, -2.0F, 3.0F},
        .rotation = glm::quat {1.0e20F, 0.0F, 0.0F, 0.0F},
        .scale = {2.0F, 3.0F, 4.0F}
    };

    const auto validated = panda::scene::detail::validateTransform(transform);

    ASSERT_TRUE(validated);
    EXPECT_EQ(validated->first.position, transform.position);
    EXPECT_EQ(validated->first.scale, transform.scale);
    expectRotationNear(validated->first.rotation, glm::quat {1.0F, 0.0F, 0.0F, 0.0F});
    const auto expected = panda::createModelMatrix({
        .position = transform.position,
        .rotation = glm::quat {1.0F, 0.0F, 0.0F, 0.0F},
        .scale = transform.scale
    });
    ASSERT_TRUE(expected);
    expectMatrixNear(validated->second, *expected);
}

TEST(SceneTransformMath, ValidateTransformRejectsZeroRotation)
{
    const auto validated = panda::scene::detail::validateTransform({
        .rotation = {0.0F, 0.0F, 0.0F, 0.0F}
    });

    ASSERT_FALSE(validated);
    EXPECT_EQ(validated.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(validated.error().message, "Transform rotation or scale is singular");
}

TEST(SceneTransformMath, DecomposeTransformPreservesReflectionAndReconstructsMatrix)
{
    const panda::Transform transform {
        .position = {4.0F,  -2.0F, 7.0F},
        .rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F,  1.0F,  0.0F}
          ),
        .scale = {-2.0F, 3.0F,  4.0F}
    };
    const auto matrix = panda::createModelMatrix(transform);
    ASSERT_TRUE(matrix);

    const auto decomposed = panda::scene::detail::decomposeTransform(*matrix);

    ASSERT_TRUE(decomposed);
    EXPECT_EQ(decomposed->first.position, transform.position);
    EXPECT_NEAR(decomposed->first.scale.x, transform.scale.x, 1.0e-5F);
    EXPECT_NEAR(decomposed->first.scale.y, transform.scale.y, 1.0e-5F);
    EXPECT_NEAR(decomposed->first.scale.z, transform.scale.z, 1.0e-5F);
    expectRotationNear(decomposed->first.rotation, transform.rotation);
    expectMatrixNear(decomposed->second, *matrix);
}

TEST(SceneTransformMath, DecomposeTransformRejectsShear)
{
    auto matrix = glm::mat4 {1.0F};
    matrix[1][0] = 0.5F;

    const auto decomposed = panda::scene::detail::decomposeTransform(matrix);

    ASSERT_FALSE(decomposed);
    EXPECT_EQ(decomposed.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(decomposed.error().message, "World transform requires unsupported shear");
}

TEST(SceneTransformMath, DecomposeTransformRejectsSingularMatrix)
{
    auto matrix = glm::mat4 {1.0F};
    matrix[2][2] = 0.0F;

    const auto decomposed = panda::scene::detail::decomposeTransform(matrix);

    ASSERT_FALSE(decomposed);
    EXPECT_EQ(decomposed.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(decomposed.error().message, "World transform is singular or non-finite");
}

TEST(SceneTransformMath, CheckedInverseReturnsInverseForFiniteTransform)
{
    const auto matrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {3.0F, -4.0F, 2.0F}) *
                        glm::mat4_cast(glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F})) *
                        glm::scale(glm::mat4 {1.0F}, glm::vec3 {2.0F, 3.0F, 4.0F});

    const auto inverse = panda::scene::detail::checkedInverse(matrix);

    ASSERT_TRUE(inverse);
    expectMatrixNear(matrix * *inverse, glm::mat4 {1.0F});
    expectMatrixNear(*inverse * matrix, glm::mat4 {1.0F});
}

TEST(SceneTransformMath, CheckedInverseRejectsSingularMatrix)
{
    auto singular = glm::mat4 {1.0F};
    singular[0][0] = 0.0F;
    const auto singularInverse = panda::scene::detail::checkedInverse(singular);

    ASSERT_FALSE(singularInverse);
    EXPECT_EQ(singularInverse.error().message, "Parent world transform is singular");
}

TEST(SceneTransformMath, CheckedInverseRejectsNonFiniteMatrix)
{
    auto nonFinite = glm::mat4 {1.0F};
    nonFinite[0][0] = std::numeric_limits<float>::infinity();
    const auto nonFiniteInverse = panda::scene::detail::checkedInverse(nonFinite);

    ASSERT_FALSE(nonFiniteInverse);
    EXPECT_EQ(nonFiniteInverse.error().message, "Parent world transform is singular");
}

TEST(SceneTransformMath, CheckedInverseRejectsNearlyDependentBasis)
{
    auto matrix = glm::mat4 {1.0F};
    matrix[1] = {1.0F, 1.0e-9F, 0.0F, 0.0F};

    const auto inverse = panda::scene::detail::checkedInverse(matrix);

    ASSERT_FALSE(inverse);
    EXPECT_EQ(inverse.error().message, "Parent world transform is singular");
}

TEST(SceneTransformMath, CheckedInverseRejectsResultOutsideFloatRange)
{
    auto matrix = glm::mat4 {1.0F};
    matrix[0][0] = std::numeric_limits<float>::denorm_min();

    const auto inverse = panda::scene::detail::checkedInverse(matrix);

    ASSERT_FALSE(inverse);
    EXPECT_EQ(inverse.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(inverse.error().message, "Parent world transform has no stable inverse");
}

TEST(SceneTransformMath, ExtractPoseRotationAcceptsPositiveUniformScale)
{
    const auto expected = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto matrix = panda::createModelMatrix({
        .position = {1.0F, 2.0F, 3.0F},
        .rotation = expected,
        .scale = {2.0F, 2.0F, 2.0F}
    });
    ASSERT_TRUE(matrix);

    const auto rotation = panda::scene::detail::extractPoseRotation(*matrix);

    ASSERT_TRUE(rotation);
    expectRotationNear(*rotation, expected);
}

TEST(SceneTransformMath, ExtractPoseRotationRejectsNonUniformScale)
{
    const auto rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto nonUniform = panda::createModelMatrix({
        .rotation = rotation,
        .scale = {2.0F, 3.0F, 2.0F}
    });
    ASSERT_TRUE(nonUniform);

    const auto extracted = panda::scene::detail::extractPoseRotation(*nonUniform);

    ASSERT_FALSE(extracted);
    EXPECT_EQ(extracted.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(extracted.error().message, "Camera and light require positive uniform world scale");
}

TEST(SceneTransformMath, ExtractPoseRotationRejectsNegativeUniformScale)
{
    const auto rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto reflected = panda::createModelMatrix({
        .rotation = rotation,
        .scale = {-2.0F, -2.0F, -2.0F}
    });
    ASSERT_TRUE(reflected);

    const auto extracted = panda::scene::detail::extractPoseRotation(*reflected);

    ASSERT_FALSE(extracted);
    EXPECT_EQ(extracted.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(extracted.error().message, "Camera and light require positive uniform world scale");
}

TEST(SceneTransformMath, ExtractPoseRotationRejectsShear)
{
    auto matrix = glm::mat4 {1.0F};
    matrix[1][0] = 0.5F;

    const auto rotation = panda::scene::detail::extractPoseRotation(matrix);

    ASSERT_FALSE(rotation);
    EXPECT_EQ(rotation.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(rotation.error().message, "World transform requires unsupported shear");
}

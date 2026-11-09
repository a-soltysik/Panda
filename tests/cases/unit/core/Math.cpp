#include "Math.hpp"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <limits>

TEST(Math, IsFiniteChecksGlmVectorsQuaternionsAndMatrices)
{
    const auto infinity = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(panda::detail::isFinite(glm::vec3 {1.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec3 {infinity}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec3 {1.0F, infinity, 1.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec3 {1.0F, 1.0F, infinity}));
    EXPECT_TRUE(panda::detail::isFinite(glm::vec4 {1.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec4 {infinity}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec4 {1.0F, infinity, 1.0F, 1.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec4 {1.0F, 1.0F, infinity, 1.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::vec4 {1.0F, 1.0F, 1.0F, infinity}));
    EXPECT_TRUE(panda::detail::isFinite(glm::quat {1.0F, 0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::quat {infinity, 0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::quat {1.0F, infinity, 0.0F, 0.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::quat {1.0F, 0.0F, infinity, 0.0F}));
    EXPECT_FALSE(panda::detail::isFinite(glm::quat {1.0F, 0.0F, 0.0F, infinity}));
    EXPECT_TRUE(panda::detail::isFinite(glm::mat3 {1.0F}));
    auto nonFiniteMatrix3 = glm::mat3 {1.0F};
    nonFiniteMatrix3[0][0] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix3));
    nonFiniteMatrix3 = glm::mat3 {1.0F};
    nonFiniteMatrix3[1][1] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix3));
    nonFiniteMatrix3 = glm::mat3 {1.0F};
    nonFiniteMatrix3[2][2] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix3));
    EXPECT_TRUE(panda::detail::isFinite(glm::mat4 {1.0F}));
    auto nonFiniteMatrix = glm::mat4 {1.0F};
    nonFiniteMatrix[0][0] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix));
    nonFiniteMatrix = glm::mat4 {1.0F};
    nonFiniteMatrix[1][1] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix));
    nonFiniteMatrix = glm::mat4 {1.0F};
    nonFiniteMatrix[2][2] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix));
    nonFiniteMatrix = glm::mat4 {1.0F};
    nonFiniteMatrix[3][3] = infinity;
    EXPECT_FALSE(panda::detail::isFinite(nonFiniteMatrix));
}

TEST(Math, IsRepresentableAsNonzeroFloatChecksFloatRange)
{
    const auto minimum = static_cast<double>(std::numeric_limits<float>::denorm_min());
    const auto maximum = static_cast<double>(std::numeric_limits<float>::max());
    EXPECT_TRUE(panda::detail::isRepresentableAsNonzeroFloat(minimum));
    EXPECT_TRUE(panda::detail::isRepresentableAsNonzeroFloat(-minimum));
    EXPECT_TRUE(panda::detail::isRepresentableAsNonzeroFloat(maximum));
    EXPECT_TRUE(panda::detail::isRepresentableAsNonzeroFloat(-maximum));
    EXPECT_FALSE(panda::detail::isRepresentableAsNonzeroFloat(0.0));
    EXPECT_FALSE(panda::detail::isRepresentableAsNonzeroFloat(minimum / 2.0));
    EXPECT_FALSE(panda::detail::isRepresentableAsNonzeroFloat(maximum * 2.0));
    EXPECT_FALSE(panda::detail::isRepresentableAsNonzeroFloat(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(panda::detail::isRepresentableAsNonzeroFloat(std::numeric_limits<double>::quiet_NaN()));
}

TEST(Math, ConvertsLinearColorChannelsToSrgb)
{
    EXPECT_FLOAT_EQ(panda::detail::linearToSrgb(0.001F), 0.01292F);
    EXPECT_NEAR(panda::detail::linearToSrgb(0.025F), 0.1718F, 0.0001F);
    EXPECT_NEAR(panda::detail::linearToSrgb(1.0F), 1.0F, 0.0001F);
}

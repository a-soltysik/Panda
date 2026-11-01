#include <gtest/gtest.h>

#include <cmath>
#include <glm/ext/vector_float3.hpp>
#include <limits>
#include <panda/Error.hpp>
#include <panda/tools/Camera.hpp>

TEST(Camera, CreatesPerspectiveRenderCamera)
{
    const auto position = glm::vec3 {3.0F, 2.5F, 4.0F};
    const auto camera = panda::tools::createPerspectiveCamera({
        .position = position,
        .target = {},
        .up = {0, 1, 0},
        .verticalFieldOfViewRadians = 0.87266463F,
        .aspectRatio = 16.0F / 9.0F,
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    });
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(camera->position.x, position.x);
    EXPECT_FLOAT_EQ(camera->position.y, position.y);
    EXPECT_FLOAT_EQ(camera->position.z, position.z);
    EXPECT_TRUE(std::isfinite(camera->viewProjection[0][0]));
    EXPECT_TRUE(std::isfinite(camera->viewProjection[3][3]));
}

TEST(Camera, RejectsDegeneratePoseAndProjection)
{
    const auto position = glm::vec3 {};
    auto description = panda::tools::PerspectiveCameraDesc {
        .position = position,
        .target = position,
        .up = {0, 1, 0},
        .verticalFieldOfViewRadians = 1.0F,
        .aspectRatio = 1.0F,
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    };
    const auto degenerateCamera = panda::tools::createPerspectiveCamera(description);
    ASSERT_FALSE(degenerateCamera);
    EXPECT_EQ(degenerateCamera.error().code, panda::ErrorCode::InvalidArgument);
    description.target = {0, 1, 0};
    description.up = {0, 2, 0};
    EXPECT_FALSE(panda::tools::createPerspectiveCamera(description));
    description.target = {0, 0, -1};
    description.up = {0, 1, 0};
    description.verticalFieldOfViewRadians = 0.0F;
    EXPECT_FALSE(panda::tools::createPerspectiveCamera(description));
    description.position = {std::numeric_limits<float>::infinity(), 0, 0};
    description.verticalFieldOfViewRadians = 1.0F;
    EXPECT_FALSE(panda::tools::createPerspectiveCamera(description));

    description.position = {};
    description.target.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(panda::tools::createPerspectiveCamera(description));
    description.target = {0, 0, -1};
    description.up.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(panda::tools::createPerspectiveCamera(description));
}

TEST(Camera, RejectsOverflowingViewProjection)
{
    const auto maximum = std::numeric_limits<float>::max();
    const auto camera = panda::tools::createPerspectiveCamera({
        .position = {maximum, 0, 0},
        .target = {maximum, 1, 0},
        .up = {0,       0, 1},
        .verticalFieldOfViewRadians = 1.0F,
        .aspectRatio = 1.0F,
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    });
    ASSERT_FALSE(camera);
    EXPECT_EQ(camera.error().code, panda::ErrorCode::InvalidArgument);
}

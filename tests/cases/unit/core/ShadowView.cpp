#include "renderer/ShadowView.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>

TEST(ShadowView, IncludesCameraTargetInDirectionalShadowVolume)
{
    auto camera = panda::RenderCamera {
        .position = {3.0F, 2.5F, 4.0F}
    };
    const auto projection = panda::createPerspectiveProjection(glm::radians(50.0F), 4.0F / 3.0F, 0.1F, 100.0F);
    ASSERT_TRUE(projection);
    camera.viewProjection = *projection * glm::lookAtRH(camera.position, glm::vec3 {}, glm::vec3 {0.0F, 1.0F, 0.0F});

    const auto lightView =
        panda::core::vulkan::renderer::createDirectionalShadowView(camera, panda::DirectionalLight {});
    ASSERT_TRUE(lightView);
    const auto target = *lightView * glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    ASSERT_NE(target.w, 0.0F);
    const auto projected = glm::vec3 {target} / target.w;
    EXPECT_LE(std::abs(projected.x), 1.0F);
    EXPECT_LE(std::abs(projected.y), 1.0F);
    EXPECT_GE(projected.z, 0.0F);
    EXPECT_LE(projected.z, 1.0F);
}

TEST(ShadowView, RejectsDirectionlessLight)
{
    auto light = panda::DirectionalLight {};
    light.direction = {};
    const auto result = panda::core::vulkan::renderer::createDirectionalShadowView(panda::RenderCamera {}, light);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(ShadowView, AcceptsLargeOrthographicCamera)
{
    auto camera = panda::RenderCamera {};
    const auto projection = panda::createOrthographicProjection(1000.0F, 1.0F, 0.1F, 1000.0F);
    ASSERT_TRUE(projection);
    camera.viewProjection = *projection;
    EXPECT_TRUE(panda::core::vulkan::renderer::createDirectionalShadowView(camera, panda::DirectionalLight {}));
}

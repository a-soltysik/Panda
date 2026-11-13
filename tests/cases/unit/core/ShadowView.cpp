#include "renderer/ShadowView.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
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

TEST(ShadowView, RejectsNonFiniteLightDirection)
{
    auto light = panda::DirectionalLight {};
    light.direction = {std::numeric_limits<float>::infinity(), 0.0F, 0.0F};

    const auto result = panda::core::vulkan::renderer::createDirectionalShadowView(panda::RenderCamera {}, light);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(ShadowView, RejectsCameraWithoutForwardDirection)
{
    const auto result =
        panda::core::vulkan::renderer::createDirectionalShadowView(panda::RenderCamera {}, panda::DirectionalLight {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(ShadowView, RejectsCameraWithSingularViewProjection)
{
    auto camera = panda::RenderCamera {};
    camera.viewProjection = glm::mat4 {0.0F};

    const auto result = panda::core::vulkan::renderer::createDirectionalShadowView(camera, panda::DirectionalLight {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(ShadowView, HandlesLightParallelToWorldUp)
{
    auto camera = panda::RenderCamera {
        .position = {3.0F, 2.5F, 4.0F}
    };
    const auto projection = panda::createPerspectiveProjection(glm::radians(50.0F), 4.0F / 3.0F, 0.1F, 100.0F);
    ASSERT_TRUE(projection);
    camera.viewProjection = *projection * glm::lookAtRH(camera.position, glm::vec3 {}, glm::vec3 {0.0F, 1.0F, 0.0F});
    auto light = panda::DirectionalLight {};
    light.direction = {0.0F, -1.0F, 0.0F};

    const auto result = panda::core::vulkan::renderer::createDirectionalShadowView(camera, light);

    EXPECT_TRUE(result);
}

TEST(ShadowView, AcceptsLargeOrthographicCamera)
{
    auto camera = panda::RenderCamera {};
    const auto projection = panda::createOrthographicProjection(1000.0F, 1.0F, 0.1F, 1000.0F);
    ASSERT_TRUE(projection);
    camera.viewProjection = *projection;
    EXPECT_TRUE(panda::core::vulkan::renderer::createDirectionalShadowView(camera, panda::DirectionalLight {}));
}

TEST(ShadowView, SpotViewCoversItsOuterCone)
{
    static constexpr auto light = panda::SpotLight {
        .position = {2.0F, 3.0F, 4.0F},
        .direction = {0.0F, 0.0F, -1.0F},
        .range = 8.0F,
        .outerConeAngleRadians = glm::radians(35.0F),
        .shadow = {}
    };

    const auto view = panda::core::vulkan::renderer::createSpotShadowView(light);

    ASSERT_TRUE(view);
    static constexpr auto distance = light.range * 0.5F;
    const auto coneEdge =
        light.position + glm::vec3 {distance * std::tan(light.outerConeAngleRadians), 0.0F, -distance};
    const auto projected = *view * glm::vec4 {coneEdge, 1.0F};
    ASSERT_NE(projected.w, 0.0F);
    EXPECT_NEAR(projected.x / projected.w, 1.0F, 1.0e-5F);
    EXPECT_NEAR(projected.y / projected.w, 0.0F, 1.0e-5F);
    EXPECT_GE(projected.z / projected.w, 0.0F);
    EXPECT_LE(projected.z / projected.w, 1.0F);
}

TEST(ShadowView, PointViewsFaceAllSixCubeDirections)
{
    static constexpr auto light = panda::PointLight {
        .position = {2.0F, 3.0F, 4.0F},
        .range = 8.0F,
        .shadow = {}
    };
    static constexpr auto directions = std::array {
        glm::vec3 {1.0F,  0.0F,  0.0F },
        glm::vec3 {-1.0F, 0.0F,  0.0F },
        glm::vec3 {0.0F,  1.0F,  0.0F },
        glm::vec3 {0.0F,  -1.0F, 0.0F },
        glm::vec3 {0.0F,  0.0F,  1.0F },
        glm::vec3 {0.0F,  0.0F,  -1.0F}
    };

    const auto views = panda::core::vulkan::renderer::createPointShadowViews(light);

    ASSERT_TRUE(views);
    for (auto index = std::size_t {}; index < views->size(); ++index)
    {
        const auto point = light.position + directions.at(index) * (light.range * 0.5F);
        const auto projected = views->at(index) * glm::vec4 {point, 1.0F};
        ASSERT_NE(projected.w, 0.0F);
        EXPECT_NEAR(projected.x / projected.w, 0.0F, 1.0e-5F);
        EXPECT_NEAR(projected.y / projected.w, 0.0F, 1.0e-5F);
        EXPECT_GE(projected.z / projected.w, 0.0F);
        EXPECT_LE(projected.z / projected.w, 1.0F);
    }
}

TEST(ShadowView, RejectsInvalidLocalShadowViews)
{
    auto spot = panda::SpotLight {.range = 8.0F, .shadow = {}};
    spot.direction = {};
    EXPECT_FALSE(panda::core::vulkan::renderer::createSpotShadowView(spot));
    spot.direction = {0.0F, 0.0F, -1.0F};
    spot.range = 0.0F;
    EXPECT_FALSE(panda::core::vulkan::renderer::createSpotShadowView(spot));

    auto point = panda::PointLight {.range = 8.0F, .shadow = {}};
    point.position.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(panda::core::vulkan::renderer::createPointShadowViews(point));
    point.position = {};
    point.range = 0.0F;
    EXPECT_FALSE(panda::core::vulkan::renderer::createPointShadowViews(point));
}

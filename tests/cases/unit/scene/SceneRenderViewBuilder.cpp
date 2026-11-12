#include "SceneRenderViewBuilder.hpp"

#include <gtest/gtest.h>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/scene/Scene.hpp>

#include "SceneState.hpp"

TEST(SceneRenderViewBuilder, RequiresActiveCamera)
{
    auto scene = panda::scene::detail::SceneState {};
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(1.0F);

    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(SceneRenderViewBuilder, BuildsOrthographicCameraAndDefaultsToNoDirectLight)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setOrthographicCamera(
        *camera,
        panda::scene::OrthographicCamera {.height = 4.0F, .nearPlane = 0.1F, .farPlane = 100.0F}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(2.0F);

    ASSERT_TRUE(view);
    EXPECT_NEAR(view->camera.viewProjection[0][0], 0.25F, 1.0e-6F);
    EXPECT_NEAR(view->camera.viewProjection[1][1], -0.5F, 1.0e-6F);
    EXPECT_TRUE(view->lights.empty());
}

TEST(SceneRenderViewBuilder, RejectsInvalidAspectAndNonuniformCameraPose)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto invalidAspect = builder.build(0.0F);
    ASSERT_FALSE(invalidAspect);
    EXPECT_EQ(invalidAspect.error().code, panda::ErrorCode::InvalidArgument);

    const auto parent = scene.createEntity({.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    ASSERT_TRUE(parent);
    ASSERT_TRUE(scene.setParent(*camera, *parent, panda::scene::ReparentMode::KeepLocal));
    const auto invalidPose = builder.build(1.0F);
    ASSERT_FALSE(invalidPose);
    EXPECT_EQ(invalidPose.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(SceneRenderViewBuilder, PreservesVisibleDirectionalLightOrder)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    const auto parent = scene.createEntity(
        {.name = {}, .transform = {.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F})}});
    ASSERT_TRUE(parent);
    const auto firstLight = scene.createEntity({});
    ASSERT_TRUE(firstLight);
    ASSERT_TRUE(scene.setLight(*firstLight, panda::scene::DirectionalLight {.intensity = 2.0F}));
    ASSERT_TRUE(scene.setParent(*firstLight, *parent, panda::scene::ReparentMode::KeepLocal));
    const auto secondLight = scene.createEntity({});
    ASSERT_TRUE(secondLight);
    ASSERT_TRUE(scene.setLight(*secondLight, panda::scene::DirectionalLight {}));
    ASSERT_TRUE(scene.setVisible(*secondLight, false));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto singleLightView = builder.build(1.0F);

    ASSERT_TRUE(singleLightView);
    ASSERT_EQ(singleLightView->lights.size(), 1U);
    const auto& singleLight = std::get<panda::DirectionalLight>(singleLightView->lights.front());
    EXPECT_NEAR(singleLight.direction.x, -1.0F, 1.0e-5F);
    EXPECT_NEAR(singleLight.direction.z, 0.0F, 1.0e-5F);
    ASSERT_TRUE(scene.setVisible(*secondLight, true));
    const auto multipleLightView = builder.build(1.0F);
    ASSERT_TRUE(multipleLightView);
    ASSERT_EQ(multipleLightView->lights.size(), 2U);
    const auto& secondDirectionalLight = std::get<panda::DirectionalLight>(multipleLightView->lights.back());
    EXPECT_NEAR(secondDirectionalLight.direction.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(secondDirectionalLight.direction.z, -1.0F, 1.0e-5F);
    ASSERT_TRUE(scene.destroyEntity(*secondLight));
    const auto singleLightViewAfterRemoval = builder.build(1.0F);
    ASSERT_TRUE(singleLightViewAfterRemoval);
    ASSERT_EQ(singleLightViewAfterRemoval->lights.size(), 1U);
}

TEST(SceneRenderViewBuilder, BuildsPointLightFromItsWorldPosition)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));

    const auto point = scene.createEntity({.name = {}, .transform = {.position = {1.0F, 2.0F, 3.0F}}});
    ASSERT_TRUE(point);
    ASSERT_TRUE(scene.setLight(*point,
                               panda::scene::PointLight {
                                   .color = {0.2F, 0.3F, 0.4F},
                                   .intensity = 2.0F,
                                   .range = 8.0F
    }));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(1.0F);

    ASSERT_TRUE(view);
    ASSERT_EQ(view->lights.size(), 1U);
    const auto& pointLight = std::get<panda::PointLight>(view->lights.front());
    EXPECT_EQ(pointLight.position, (glm::vec3 {1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(pointLight.color, (glm::vec3 {0.2F, 0.3F, 0.4F}));
    EXPECT_EQ(pointLight.intensity, 2.0F);
    EXPECT_EQ(pointLight.range, 8.0F);
}

TEST(SceneRenderViewBuilder, BuildsSpotLightFromItsWorldPose)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));

    const auto spot = scene.createEntity({
        .name = {},
        .transform = {.position = {4.0F, 5.0F, 6.0F},
                 .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F})}
    });
    ASSERT_TRUE(spot);
    ASSERT_TRUE(scene.setLight(*spot,
                               panda::scene::SpotLight {
                                   .color = {0.5F, 0.6F, 0.7F},
                                   .intensity = 3.0F,
                                   .range = 12.0F,
                                   .innerConeAngleRadians = 0.2F,
                                   .outerConeAngleRadians = 0.4F
    }));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(1.0F);

    ASSERT_TRUE(view);
    ASSERT_EQ(view->lights.size(), 1U);
    const auto& spotLight = std::get<panda::SpotLight>(view->lights.front());
    EXPECT_EQ(spotLight.position, (glm::vec3 {4.0F, 5.0F, 6.0F}));
    EXPECT_NEAR(spotLight.direction.x, -1.0F, 1.0e-5F);
    EXPECT_NEAR(spotLight.direction.z, 0.0F, 1.0e-5F);
    EXPECT_EQ(spotLight.color, (glm::vec3 {0.5F, 0.6F, 0.7F}));
    EXPECT_EQ(spotLight.intensity, 3.0F);
    EXPECT_EQ(spotLight.range, 12.0F);
    EXPECT_EQ(spotLight.innerConeAngleRadians, 0.2F);
    EXPECT_EQ(spotLight.outerConeAngleRadians, 0.4F);
}

TEST(SceneRenderViewBuilder, RejectsLightWithNonuniformWorldPose)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = scene.createEntity({});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    const auto parent = scene.createEntity({.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    ASSERT_TRUE(parent);
    const auto light = scene.createEntity({});
    ASSERT_TRUE(light);
    ASSERT_TRUE(scene.setLight(*light, panda::scene::DirectionalLight {}));
    ASSERT_TRUE(scene.setParent(*light, *parent, panda::scene::ReparentMode::KeepLocal));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(1.0F);

    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(SceneRenderViewBuilder, PropagatesUnrepresentableWorldTransform)
{
    auto scene = panda::scene::detail::SceneState {};
    static constexpr auto maximumScale = std::numeric_limits<float>::max();
    const auto parent = scene.createEntity({.name = {}, .transform = {.scale = glm::vec3 {maximumScale}}});
    ASSERT_TRUE(parent);
    const auto camera = scene.createEntity({.name = {}, .transform = {.scale = glm::vec3 {maximumScale}}});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    ASSERT_TRUE(scene.setParent(*camera, *parent, panda::scene::ReparentMode::KeepLocal));
    auto builder = panda::scene::detail::SceneRenderViewBuilder {scene};

    const auto view = builder.build(1.0F);

    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, panda::ErrorCode::InvalidArgument);
}

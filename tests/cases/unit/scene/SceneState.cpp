#include "SceneState.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/Transform.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/Scene.hpp>

namespace
{
auto createObject(panda::scene::detail::SceneState& scene, const panda::scene::EntityDesc& description = {})
    -> panda::scene::Entity
{
    auto entity = scene.createEntity(description);
    EXPECT_TRUE(entity);
    return entity ? *entity : panda::scene::Entity {};
}

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
}

TEST(SceneState, RejectsStaleAndCrossSceneHandlesAfterSlotReuse)
{
    auto firstScene = panda::scene::detail::SceneState {};
    auto secondScene = panda::scene::detail::SceneState {};
    const auto stale = createObject(firstScene);
    ASSERT_TRUE(firstScene.destroyEntity(stale));
    const auto replacement = createObject(firstScene);

    EXPECT_FALSE(firstScene.isAlive(stale));
    EXPECT_TRUE(firstScene.isAlive(replacement));
    EXPECT_FALSE(secondScene.isAlive(replacement));
    EXPECT_FALSE(firstScene.getLocalTransform(stale));
}

TEST(SceneState, GeneratesStableObjectIdsAndRejectsReuse)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto explicitId = panda::scene::ObjectId {0x0123456789abcdefULL};
    EXPECT_FALSE(scene.getObjectId(panda::scene::Entity {}));
    EXPECT_FALSE(scene.findEntity(panda::scene::ObjectId {0}));
    EXPECT_FALSE(scene.createEntity(
        explicitId,
        {.name = {}, .transform = {.position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}}}));

    const auto object = scene.createEntity(explicitId, {.name = "explicit"});
    ASSERT_TRUE(object);
    const auto resolvedId = scene.getObjectId(*object);
    ASSERT_TRUE(resolvedId);
    EXPECT_EQ(*resolvedId, explicitId);
    const auto foundObject = scene.findEntity(explicitId);
    if (foundObject)
    {
        EXPECT_EQ(*foundObject, *object);
    }
    else
    {
        FAIL() << "The explicit ObjectId should resolve while its entity is alive";
    }
    EXPECT_FALSE(scene.createEntity(explicitId, {.name = "duplicate"}));
    ASSERT_TRUE(scene.destroyEntity(*object));
    EXPECT_FALSE(scene.findEntity(explicitId));
    EXPECT_FALSE(scene.createEntity(explicitId, {.name = "reused"}));

    const auto generated = createObject(scene);
    const auto generatedId = scene.getObjectId(generated);
    ASSERT_TRUE(generatedId);
    EXPECT_NE(*generatedId, explicitId);
    ASSERT_TRUE(scene.destroyEntity(generated));
    const auto replacement = createObject(scene);
    const auto replacementId = scene.getObjectId(replacement);
    ASSERT_TRUE(replacementId);
    EXPECT_NE(*replacementId, *generatedId);
    EXPECT_FALSE(scene.findEntity(*generatedId));
}

TEST(SceneState, AdvancesGeneratedObjectIdPastExplicitCollision)
{
    auto scene = panda::scene::detail::SceneState {};
    ASSERT_TRUE(scene.createEntity(panda::scene::ObjectId {1}, {.name = "explicit"}));

    const auto generated = createObject(scene);
    const auto generatedId = scene.getObjectId(generated);

    ASSERT_TRUE(generatedId);
    EXPECT_EQ(generatedId->value(), 2);
}

TEST(ObjectId, RetainsItsValueAndOrdersNumerically)
{
    const auto zero = panda::scene::ObjectId {0};
    const auto larger = panda::scene::ObjectId {0x0123456789abcdefULL};
    EXPECT_EQ(zero.value(), 0);
    EXPECT_EQ(larger.value(), 0x0123456789abcdefULL);
    EXPECT_LT(zero, larger);
}

TEST(SceneState, EditsNamesAndWorldTransformOnRootAndRejectsEmptyHandles)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto empty = panda::scene::Entity {};
    EXPECT_FALSE(scene.isAlive(empty));
    EXPECT_FALSE(scene.getName(empty));
    EXPECT_FALSE(scene.setName(empty, "ignored"));
    EXPECT_FALSE(scene.setWorldTransform(empty, {}));

    const auto root = createObject(scene, {.name = "Root"});
    ASSERT_TRUE(scene.setName(root, "Renamed"));
    const auto name = scene.getName(root);
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "Renamed");
    const auto parent = scene.getParent(root);
    ASSERT_TRUE(parent);
    EXPECT_FALSE(*parent);

    const auto rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F});
    const panda::Transform requested {
        .position = {3.0F, -2.0F, 5.0F},
        .rotation = rotation
    };
    const auto expected = panda::createModelMatrix(requested);
    ASSERT_TRUE(expected);
    ASSERT_TRUE(scene.setWorldTransform(root, requested));
    const auto world = scene.getWorldTransform(root);
    ASSERT_TRUE(world);
    expectMatrixNear(*world, *expected);

    const auto invalid =
        scene.setWorldTransform(root,
                                {
                                    .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}
    });
    EXPECT_FALSE(invalid);
    const auto unchanged = scene.getWorldTransform(root);
    ASSERT_TRUE(unchanged);
    expectMatrixNear(*unchanged, *expected);
}

TEST(SceneState, RejectsForeignParentsInvalidModesAndOverflowingWorldTransforms)
{
    auto scene = panda::scene::detail::SceneState {};
    auto otherScene = panda::scene::detail::SceneState {};
    const auto child = createObject(scene);
    const auto foreignParent = createObject(otherScene);
    EXPECT_FALSE(scene.setParent(child, foreignParent, panda::scene::ReparentMode::KeepLocal));
    EXPECT_FALSE(scene.destroySubtree(panda::scene::Entity {}));

    const auto hugeScale = glm::vec3 {1.0e20F, 1.0e20F, 1.0e20F};
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = hugeScale}});
    const auto overflowingChild = createObject(scene, {.name = {}, .transform = {.scale = hugeScale}});
    ASSERT_TRUE(scene.setParent(overflowingChild, parent, panda::scene::ReparentMode::KeepLocal));
    EXPECT_FALSE(scene.getWorldTransform(overflowingChild));

    const auto editableChild = createObject(scene);
    ASSERT_TRUE(scene.setParent(editableChild, parent, panda::scene::ReparentMode::KeepLocal));
    const auto before = scene.getLocalTransform(editableChild);
    ASSERT_TRUE(before);
    EXPECT_FALSE(scene.setLocalTransform(editableChild, {.scale = hugeScale}));
    const auto after = scene.getLocalTransform(editableChild);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->scale, before->scale);
}

TEST(SceneState, CachesWorldTransformsAndAllowsShearInWorldMatrices)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto parent = createObject(scene, {.name = {}, .transform = {.position = {3.0F, 2.0F, 1.0F}}});
    const auto child = createObject(
        scene,
        {.name = {},
         .transform = {.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 0.0F, 1.0F})}});
    ASSERT_TRUE(scene.setLocalTransform(parent,
                                        {
                                            .position = {5.0F, 0.0F, 0.0F},
                                            .scale = {2.0F, 1.0F, 1.0F}
    }));
    ASSERT_TRUE(scene.setParent(child, parent, panda::scene::ReparentMode::KeepLocal));
    const auto world = scene.getWorldTransform(child);
    ASSERT_TRUE(world);
    const auto firstBasis = glm::vec3 {(*world)[0]};
    const auto secondBasis = glm::vec3 {(*world)[1]};
    EXPECT_GT(std::abs(glm::dot(glm::normalize(firstBasis), glm::normalize(secondBasis))), 1.0e-3F);

    ASSERT_TRUE(scene.setLocalTransform(parent,
                                        {
                                            .position = {7.0F, 0.0F, 0.0F}
    }));
    const auto updated = scene.getWorldTransform(child);
    ASSERT_TRUE(updated);
    EXPECT_NEAR((*updated)[3].x, 7.0F, 1.0e-6F);
}

TEST(SceneState, RejectsKeepWorldReparentThatWouldRequireShearWithoutChangingParent)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto child = createObject(
        scene,
        {.name = {},
         .transform = {.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 0.0F, 1.0F})}});
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    const auto originalWorld = scene.getWorldTransform(child);
    ASSERT_TRUE(originalWorld);

    const auto result = scene.setParent(child, parent, panda::scene::ReparentMode::KeepWorld);

    EXPECT_FALSE(result);
    EXPECT_FALSE(scene.getParent(child).value());
    const auto worldAfterFailure = scene.getWorldTransform(child);
    ASSERT_TRUE(worldAfterFailure);
    EXPECT_EQ(*worldAfterFailure, *originalWorld);
}

TEST(SceneState, RejectsCyclesAndSingleObjectDeletionWithChildren)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto parent = createObject(scene);
    const auto child = createObject(scene);
    ASSERT_TRUE(scene.setParent(child, parent, panda::scene::ReparentMode::KeepLocal));

    EXPECT_FALSE(scene.setParent(parent, child, panda::scene::ReparentMode::KeepLocal));
    EXPECT_FALSE(scene.destroyEntity(parent));
    ASSERT_TRUE(scene.destroySubtree(parent));
    EXPECT_FALSE(scene.isAlive(parent));
    EXPECT_FALSE(scene.isAlive(child));
}

TEST(SceneState, RejectsInvalidTransformWithoutReplacingPreviousValue)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto entity = createObject(scene, {.name = {}, .transform = {.position = {1.0F, 2.0F, 3.0F}}});
    const auto before = scene.getLocalTransform(entity);
    ASSERT_TRUE(before);

    const auto invalid =
        scene.setLocalTransform(entity,
                                {
                                    .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}
    });

    EXPECT_FALSE(invalid);
    const auto after = scene.getLocalTransform(entity);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->position, before->position);
}

TEST(SceneState, SetsWorldTransformRelativeToTranslatedRotatedParent)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto parentRotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto parent = createObject(
        scene,
        {
            .name = {},
            .transform = {.position = {3.0F, -2.0F, 1.0F}, .rotation = parentRotation, .scale = glm::vec3 {2.0F}}
    });
    const auto child = createObject(scene);
    ASSERT_TRUE(scene.setParent(child, parent, panda::scene::ReparentMode::KeepLocal));
    const auto requestedRotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {1.0F, 0.0F, 0.0F});
    const panda::Transform requested {
        .position = {8.0F, 4.0F, -3.0F},
        .rotation = requestedRotation,
        .scale = glm::vec3 {1.5F}
    };
    const auto expected = panda::createModelMatrix(requested);
    ASSERT_TRUE(expected);

    ASSERT_TRUE(scene.setWorldTransform(child, requested));

    const auto world = scene.getWorldTransform(child);
    ASSERT_TRUE(world);
    expectMatrixNear(*world, *expected);
}

TEST(SceneState, RejectsWorldTransformThatWouldRequireLocalShearWithoutChangingTransform)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    const auto child = createObject(scene, {.name = {}, .transform = {.position = {0.5F, 1.0F, -2.0F}}});
    ASSERT_TRUE(scene.setParent(child, parent, panda::scene::ReparentMode::KeepLocal));
    const auto localBefore = scene.getLocalTransform(child);
    const auto worldBefore = scene.getWorldTransform(child);
    ASSERT_TRUE(localBefore);
    ASSERT_TRUE(worldBefore);
    const auto requestedRotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 0.0F, 1.0F});
    const panda::Transform requested {
        .position = {4.0F, 1.0F, 2.0F},
        .rotation = requestedRotation
    };

    const auto result = scene.setWorldTransform(child, requested);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
    const auto localAfter = scene.getLocalTransform(child);
    const auto worldAfter = scene.getWorldTransform(child);
    ASSERT_TRUE(localAfter);
    ASSERT_TRUE(worldAfter);
    EXPECT_EQ(localAfter->position, localBefore->position);
    EXPECT_EQ(localAfter->rotation, localBefore->rotation);
    EXPECT_EQ(localAfter->scale, localBefore->scale);
    expectMatrixNear(*worldAfter, *worldBefore);
}

namespace
{
auto selectCamera(panda::scene::detail::SceneState& scene) -> panda::scene::Entity
{
    const auto camera = createObject(scene);
    EXPECT_TRUE(scene.setCamera(camera, panda::scene::PerspectiveCamera {}));
    EXPECT_TRUE(scene.setActiveCamera(camera));
    return camera;
}
}

TEST(SceneState, RequiresActiveCameraAndClearsSelectionOnDeletion)
{
    auto scene = panda::scene::detail::SceneState {};
    EXPECT_FALSE(scene.createRenderView(1.0F));
    const auto camera = selectCamera(scene);
    ASSERT_TRUE(scene.createRenderView(1.0F));
    ASSERT_TRUE(scene.destroyEntity(camera));
    const auto replacement = createObject(scene);
    ASSERT_TRUE(scene.setCamera(replacement, panda::scene::PerspectiveCamera {}));
    EXPECT_FALSE(scene.createRenderView(1.0F));
}

TEST(SceneState, TransformsParentedLightAndKeepsReturnedSnapshotIndependent)
{
    auto scene = panda::scene::detail::SceneState {};
    selectCamera(scene);
    const auto parent = createObject(
        scene,
        {.name = {}, .transform = {.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F})}});
    const auto light = createObject(scene);
    ASSERT_TRUE(scene.setLight(light,
                               panda::scene::DirectionalLight {
                                   .color = {0.5F, 0.6F, 0.7F},
                                   .intensity = 2.0F,
                                   .shadow = {}
    }));
    ASSERT_TRUE(scene.setParent(light, parent, panda::scene::ReparentMode::KeepLocal));
    ASSERT_TRUE(scene.setVisible(parent, false));
    const auto first = scene.createRenderView(1.0F);
    ASSERT_TRUE(first);
    ASSERT_EQ(first->lights.size(), 1U);
    const auto& firstLight = std::get<panda::DirectionalLight>(first->lights.front());
    EXPECT_NEAR(firstLight.direction.x, -1.0F, 1.0e-5F);
    EXPECT_NEAR(firstLight.direction.z, 0.0F, 1.0e-5F);
    EXPECT_EQ(firstLight.intensity, 2.0F);
    ASSERT_TRUE(scene.setVisible(light, false));
    const auto second = scene.createRenderView(1.0F);
    ASSERT_TRUE(second);
    EXPECT_TRUE(second->lights.empty());
    EXPECT_EQ(std::get<panda::DirectionalLight>(first->lights.front()).intensity, 2.0F);
}

TEST(SceneState, CollectsVisibleLightsAndRejectsInvalidEdits)
{
    auto scene = panda::scene::detail::SceneState {};
    selectCamera(scene);
    const auto first = createObject(scene);
    const auto second = createObject(scene);
    ASSERT_TRUE(scene.setLight(first, panda::scene::DirectionalLight {.intensity = 2.0F, .shadow = {}}));
    EXPECT_FALSE(scene.setLight(first, panda::scene::DirectionalLight {.intensity = -1.0F, .shadow = {}}));
    EXPECT_FALSE(scene.setLight(first,
                                panda::scene::DirectionalLight {
                                    .color = {-1.0F, 0.0F, 0.0F},
                                    .shadow = {}
    }));
    EXPECT_FALSE(scene.setLight(
        first,
        panda::scene::DirectionalLight {.intensity = std::numeric_limits<float>::quiet_NaN(), .shadow = {}}));
    EXPECT_FALSE(
        scene.setLight(first, panda::scene::DirectionalLight {.shadow = panda::ShadowSettings {.resolution = 768U}}));
    EXPECT_FALSE(scene.setLight(
        first,
        panda::scene::DirectionalLight {.shadow = panda::ShadowSettings {}, .shadowCoverageMeters = 0.0F}));
    auto view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    ASSERT_EQ(view->lights.size(), 1U);
    EXPECT_EQ(std::get<panda::DirectionalLight>(view->lights.front()).intensity, 2.0F);
    ASSERT_TRUE(scene.setLight(second, panda::scene::DirectionalLight {}));
    view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    ASSERT_EQ(view->lights.size(), 2U);
    EXPECT_EQ(std::get<panda::DirectionalLight>(view->lights.back()).intensity, 3.0F);
    ASSERT_TRUE(scene.setLight(second, std::nullopt));
    view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    ASSERT_EQ(view->lights.size(), 1U);
}

TEST(SceneState, ValidatesPointLightParameters)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto point = createObject(scene);
    ASSERT_TRUE(scene.setLight(point, panda::scene::PointLight {.range = 8.0F, .shadow = {}}));
    EXPECT_FALSE(scene.setLight(point, panda::scene::PointLight {.range = 0.0F, .shadow = {}}));
    EXPECT_FALSE(
        scene.setLight(point,
                       panda::scene::PointLight {.range = std::numeric_limits<float>::infinity(), .shadow = {}}));
    EXPECT_FALSE(scene.setLight(
        point,
        panda::scene::PointLight {.intensity = std::numeric_limits<float>::infinity(), .range = 8.0F, .shadow = {}}));
    EXPECT_FALSE(scene.setLight(point,
                                panda::scene::PointLight {
                                    .color = {std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F},
                                    .range = 8.0F,
                                    .shadow = {}
    }));
    EXPECT_FALSE(scene.setLight(point,
                                panda::scene::PointLight {
                                    .color = {1.0F, -1.0F, 1.0F},
                                    .range = 8.0F,
                                    .shadow = {}
    }));
    EXPECT_FALSE(scene.setLight(point,
                                panda::scene::PointLight {
                                    .range = 8.0F,
                                    .shadow = panda::ShadowSettings {.resolution = {}, .normalOffset = -1.0F}
    }));
}

TEST(SceneState, ValidatesSpotLightParameters)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto spot = createObject(scene);
    ASSERT_TRUE(scene.setLight(spot,
                               panda::scene::SpotLight {.range = 8.0F,
                                                        .innerConeAngleRadians = 0.2F,
                                                        .outerConeAngleRadians = 0.4F,
                                                        .shadow = {}}));
    EXPECT_FALSE(scene.setLight(spot, panda::scene::SpotLight {.range = 0.0F, .shadow = {}}));
    EXPECT_FALSE(
        scene.setLight(spot, panda::scene::SpotLight {.range = std::numeric_limits<float>::quiet_NaN(), .shadow = {}}));
    EXPECT_FALSE(scene.setLight(spot,
                                panda::scene::SpotLight {
                                    .color = {-1.0F, 1.0F, 1.0F},
                                    .range = 8.0F,
                                    .innerConeAngleRadians = 0.2F,
                                    .outerConeAngleRadians = 0.4F,
                                    .shadow = {}
    }));
    EXPECT_FALSE(scene.setLight(spot,
                                panda::scene::SpotLight {.range = 8.0F,
                                                         .innerConeAngleRadians = -0.1F,
                                                         .outerConeAngleRadians = 0.4F,
                                                         .shadow = {}}));
    EXPECT_FALSE(scene.setLight(spot,
                                panda::scene::SpotLight {.range = 8.0F,
                                                         .innerConeAngleRadians = 0.4F,
                                                         .outerConeAngleRadians = 0.4F,
                                                         .shadow = {}}));
    EXPECT_FALSE(scene.setLight(spot,
                                panda::scene::SpotLight {.range = 8.0F,
                                                         .innerConeAngleRadians = 0.2F,
                                                         .outerConeAngleRadians = glm::half_pi<float>() + 0.1F,
                                                         .shadow = {}}));
    EXPECT_FALSE(
        scene.setLight(spot,
                       panda::scene::SpotLight {.range = 8.0F,
                                                .innerConeAngleRadians = std::numeric_limits<float>::quiet_NaN(),
                                                .outerConeAngleRadians = 0.4F,
                                                .shadow = {}}));
    EXPECT_FALSE(
        scene.setLight(spot,
                       panda::scene::SpotLight {.range = 8.0F,
                                                .innerConeAngleRadians = 0.2F,
                                                .outerConeAngleRadians = std::numeric_limits<float>::infinity(),
                                                .shadow = {}}));
    EXPECT_FALSE(scene.setLight(
        spot,
        panda::scene::SpotLight {
            .range = 8.0F,
            .shadow = panda::ShadowSettings {.resolution = {}, .depthBias = std::numeric_limits<float>::quiet_NaN()}
    }));
}

TEST(SceneState, ValidatesCameraLensAspectAndInheritedPose)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = selectCamera(scene);
    EXPECT_FALSE(scene.createRenderView(0.0F));
    EXPECT_FALSE(scene.setCamera(camera, panda::scene::PerspectiveCamera {.nearPlane = -1.0F}));
    EXPECT_FALSE(scene.setCamera(
        camera,
        panda::scene::PerspectiveCamera {.verticalFieldOfViewRadians = std::numeric_limits<float>::quiet_NaN()}));
    EXPECT_FALSE(scene.setCamera(camera, panda::scene::PerspectiveCamera {.nearPlane = 2.0F, .farPlane = 2.0F}));
    EXPECT_TRUE(scene.createRenderView(1.0F));
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    ASSERT_TRUE(scene.setParent(camera, parent, panda::scene::ReparentMode::KeepLocal));
    EXPECT_FALSE(scene.createRenderView(1.0F));
    ASSERT_TRUE(scene.setLocalTransform(parent,
                                        {
                                            .position = {3.0F, 2.0F, 1.0F},
                                            .scale = {2.0F, 2.0F, 2.0F}
    }));
    const auto view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    EXPECT_EQ(view->camera.position, (glm::vec3 {3.0F, 2.0F, 1.0F}));
    ASSERT_TRUE(scene.setCamera(camera, std::nullopt));
    EXPECT_FALSE(scene.createRenderView(1.0F));
}

TEST(SceneState, BuildsOrthographicCameraAndSwitchesLensWithoutLosingSelection)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = createObject(scene);
    const auto inactiveCamera = createObject(scene);
    ASSERT_TRUE(scene.setOrthographicCamera(
        camera,
        panda::scene::OrthographicCamera {.height = 4.0F, .nearPlane = 0.1F, .farPlane = 100.0F}));
    ASSERT_TRUE(scene.setActiveCamera(camera));

    const auto orthographic = scene.createRenderView(2.0F);
    ASSERT_TRUE(orthographic);
    EXPECT_NEAR(orthographic->camera.viewProjection[0][0], 0.25F, 1.0e-6F);
    EXPECT_NEAR(orthographic->camera.viewProjection[1][1], -0.5F, 1.0e-6F);
    const auto nearClip = orthographic->camera.viewProjection * glm::vec4 {0.0F, 0.0F, -0.1F, 1.0F};
    const auto farClip = orthographic->camera.viewProjection * glm::vec4 {0.0F, 0.0F, -100.0F, 1.0F};
    EXPECT_NEAR(nearClip.z / nearClip.w, 0.0F, 1.0e-6F);
    EXPECT_NEAR(farClip.z / farClip.w, 1.0F, 1.0e-6F);

    ASSERT_TRUE(scene.setCamera(inactiveCamera, std::nullopt));
    ASSERT_TRUE(scene.setOrthographicCamera(inactiveCamera, std::nullopt));
    EXPECT_TRUE(scene.createRenderView(2.0F));

    EXPECT_FALSE(scene.setOrthographicCamera(
        camera,
        panda::scene::OrthographicCamera {.height = std::numeric_limits<float>::quiet_NaN()}));
    ASSERT_TRUE(scene.setCamera(camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setOrthographicCamera(camera, panda::scene::OrthographicCamera {.height = 4.0F}));
    ASSERT_TRUE(scene.setCamera(camera, std::nullopt));
    EXPECT_TRUE(scene.createRenderView(2.0F));
    ASSERT_TRUE(scene.setOrthographicCamera(camera, std::nullopt));
    EXPECT_FALSE(scene.createRenderView(2.0F));

    ASSERT_TRUE(scene.setCamera(camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(camera));
    ASSERT_TRUE(scene.setCamera(camera, std::nullopt));
    EXPECT_FALSE(scene.createRenderView(2.0F));
}

TEST(SceneState, RejectsUnrepresentableCameraProjectionAndViewMatrices)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = selectCamera(scene);
    const auto tinyAspect = std::numeric_limits<float>::denorm_min();
    EXPECT_FALSE(scene.createRenderView(tinyAspect));

    ASSERT_TRUE(scene.setOrthographicCamera(camera, panda::scene::OrthographicCamera {}));
    EXPECT_FALSE(scene.createRenderView(tinyAspect));

    ASSERT_TRUE(scene.setCamera(camera, panda::scene::PerspectiveCamera {}));
    const auto largestPosition = std::numeric_limits<float>::max();
    ASSERT_TRUE(scene.setLocalTransform(camera,
                                        {
                                            .position = {largestPosition, 0.0F, 0.0F}
    }));
    const auto view = scene.createRenderView(1.0F);
    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(SceneState, RejectsDirectionalLightWithNonUniformWorldPose)
{
    auto scene = panda::scene::detail::SceneState {};
    selectCamera(scene);
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = {2.0F, 1.0F, 1.0F}}});
    const auto light = createObject(scene);
    ASSERT_TRUE(scene.setLight(light, panda::scene::DirectionalLight {}));
    ASSERT_TRUE(scene.setParent(light, parent, panda::scene::ReparentMode::KeepLocal));

    const auto view = scene.createRenderView(1.0F);

    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(SceneState, RejectsForeignComponentHandlesAndEmptyMeshes)
{
    auto scene = panda::scene::detail::SceneState {};
    auto other = panda::scene::detail::SceneState {};
    const auto foreign = createObject(other);
    const auto local = createObject(scene);
    EXPECT_FALSE(scene.setCamera(foreign, panda::scene::PerspectiveCamera {}));
    EXPECT_FALSE(scene.setOrthographicCamera(foreign, panda::scene::OrthographicCamera {}));
    EXPECT_FALSE(scene.setLight(foreign, panda::scene::DirectionalLight {}));
    EXPECT_FALSE(scene.setVisible(foreign, false));
    EXPECT_FALSE(scene.setMesh(local, panda::scene::MeshInstance {}));
    EXPECT_FALSE(scene.setActiveCamera(local));
}

TEST(Entity, ComparesSceneSlotReuseAndExpiredIdentity)
{
    auto scene = panda::scene::detail::SceneState {};
    auto otherScene = panda::scene::detail::SceneState {};
    const auto first = createObject(scene);
    const auto otherSlot = createObject(scene);
    const auto foreign = createObject(otherScene);

    EXPECT_EQ(first, first);
    EXPECT_NE(first, otherSlot);
    EXPECT_NE(first, foreign);
    EXPECT_NE(foreign, first);

    ASSERT_TRUE(scene.destroyEntity(first));
    const auto replacement = createObject(scene);
    EXPECT_NE(first, replacement);

    const auto expired = [] {
        auto temporary = panda::scene::detail::SceneState {};
        return createObject(temporary);
    }();
    EXPECT_NE(expired, panda::scene::Entity {});
    EXPECT_NE(panda::scene::Entity {}, expired);
}

TEST(SceneState, IgnoresSmallPositiveUniformWorldScaleForCameraLens)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto camera = selectCamera(scene);
    const auto original = scene.createRenderView(1.0F);
    ASSERT_TRUE(original);
    const auto parent = createObject(scene, {.name = {}, .transform = {.scale = {1.0e-6F, 1.0e-6F, 1.0e-6F}}});
    ASSERT_TRUE(scene.setLocalTransform(camera,
                                        {
                                            .scale = {1.0e-6F, 1.0e-6F, 1.0e-6F}
    }));
    ASSERT_TRUE(scene.setParent(camera, parent, panda::scene::ReparentMode::KeepLocal));
    const auto scaled = scene.createRenderView(1.0F);
    ASSERT_TRUE(scaled);
    EXPECT_EQ(scaled->camera.viewProjection, original->camera.viewProjection);
}

TEST(SceneState, KeepsWorldTransformUnderReflectedUniformParent)
{
    auto scene = panda::scene::detail::SceneState {};
    const auto child = createObject(
        scene,
        {
            .name = {},
            .transform = {.position = {1.0F, 2.0F, 3.0F},
                     .rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3 {0.0F, 1.0F, 0.0F})}
    });
    const auto parent = createObject(scene,
                                     {
                                         .name = {},
                                         .transform = {.position = {4.0F, 0.0F, 0.0F}, .scale = {-2.0F, 2.0F, 2.0F}}
    });
    const auto original = scene.getWorldTransform(child);
    ASSERT_TRUE(original);
    ASSERT_TRUE(scene.setParent(child, parent, panda::scene::ReparentMode::KeepWorld));
    const auto local = scene.getLocalTransform(child);
    ASSERT_TRUE(local);
    EXPECT_LT(local->scale.x, 0.0F);
    const auto updated = scene.getWorldTransform(child);
    ASSERT_TRUE(updated);
    expectMatrixNear(*updated, *original);
}

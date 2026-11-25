#include <gtest/gtest.h>

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/matrix.hpp>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/Scene.hpp>
#include <utility>

#include "TestGeometry.hpp"

namespace
{
constexpr auto renderExtent = glm::uvec2 {160, 120};

class SceneRenderingSystem : public testing::Test
{
protected:
    static void selectCamera(panda::scene::Scene& scene)
    {
        const auto camera = scene.createEntity({.name = "Camera"});
        ASSERT_TRUE(camera);
        ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
        ASSERT_TRUE(scene.setActiveCamera(*camera));
    }

    void SetUp() override
    {
        auto created = panda::Engine::create({.applicationName = "Panda headless scene", .enableValidation = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        context.emplace(std::move(*created));
        auto target = context->getResourceManager().createOffscreenTarget({.extent = renderExtent});
        ASSERT_TRUE(target) << target.error().message;
        renderTarget = std::move(*target);
    }

    auto activeEngine() -> panda::Engine&
    {
        return panda::expect(context, "Scene system test has no initialized Engine");
    }

    std::optional<panda::Engine> context;
    panda::OffscreenTarget renderTarget;
};
}

TEST_F(SceneRenderingSystem, BuildsHeadlessSnapshotAndRetainsDeletedObjectMesh)
{
    auto scene = panda::scene::Scene {};

    selectCamera(scene);
    auto mesh = activeEngine().getResourceManager().createMeshFromGeometry(panda::test::makeTestTriangleGeometry());
    ASSERT_TRUE(mesh);
    const auto object = scene.createEntity({.name = {}, .transform = {.position = {2.0F, 0.0F, 0.0F}}});
    ASSERT_TRUE(object);
    auto meshInstance = panda::scene::MeshInstance {.mesh = *mesh};
    meshInstance.material.metallic = 0.75F;
    meshInstance.material.roughness = 0.25F;
    ASSERT_TRUE(scene.setMesh(*object, meshInstance));
    auto moved = std::move(scene);
    EXPECT_TRUE(moved.isAlive(*object));
    const auto view = moved.createRenderView(1.5F);
    ASSERT_TRUE(view) << view.error().message;
    ASSERT_EQ(view->meshes.size(), 1);
    EXPECT_EQ(view->meshes.front().instances.front().modelMatrix[3].x, 2.0F);
    EXPECT_FLOAT_EQ(view->meshes.front().material.metallic, 0.75F);
    EXPECT_FLOAT_EQ(view->meshes.front().material.roughness, 0.25F);
    ASSERT_TRUE(moved.destroyEntity(*object));
    mesh->reset();
    EXPECT_TRUE(view->meshes.front().mesh.isValid());
    const auto empty = moved.createRenderView(1.5F);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->meshes.empty());
}

TEST_F(SceneRenderingSystem, BatchesCompatibleOpaqueObjectsAndKeepsBlendOrdering)
{
    auto scene = panda::scene::Scene {};
    selectCamera(scene);
    const auto mesh =
        activeEngine().getResourceManager().createMeshFromGeometry(panda::test::makeTestTriangleGeometry());
    ASSERT_TRUE(mesh);

    const auto first = scene.createEntity({.name = {}, .transform = {.position = {-1.0F, 0.0F, 0.0F}}});
    const auto second = scene.createEntity({.name = {}, .transform = {.position = {1.0F, 0.0F, 0.0F}}});
    const auto differentMaterial = scene.createEntity({});
    const auto mirrored = scene.createEntity({.name = {}, .transform = {.scale = {-1.0F, 1.0F, 1.0F}}});
    const auto firstBlend = scene.createEntity({.name = {}, .transform = {.position = {-0.5F, 0.0F, 0.0F}}});
    const auto secondBlend = scene.createEntity({.name = {}, .transform = {.position = {0.5F, 0.0F, 0.0F}}});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(differentMaterial);
    ASSERT_TRUE(mirrored);
    ASSERT_TRUE(firstBlend);
    ASSERT_TRUE(secondBlend);

    ASSERT_TRUE(scene.setMesh(*first,
                              panda::scene::MeshInstance {
                                  .mesh = *mesh,
                                  .tint = {1.0F, 0.0F, 0.0F, 1.0F}
    }));
    ASSERT_TRUE(scene.setMesh(*second,
                              panda::scene::MeshInstance {
                                  .mesh = *mesh,
                                  .tint = {0.0F, 1.0F, 0.0F, 1.0F}
    }));
    auto changedMaterial = panda::scene::MeshInstance {.mesh = *mesh};
    changedMaterial.material.baseColor = {0.5F, 0.5F, 0.5F, 1.0F};
    ASSERT_TRUE(scene.setMesh(*differentMaterial, changedMaterial));
    ASSERT_TRUE(scene.setMesh(*mirrored, panda::scene::MeshInstance {.mesh = *mesh}));
    auto blended = panda::scene::MeshInstance {.mesh = *mesh};
    blended.material.alphaMode = panda::AlphaMode::Blend;
    ASSERT_TRUE(scene.setMesh(*firstBlend, blended));
    ASSERT_TRUE(scene.setMesh(*secondBlend, blended));

    const auto view = scene.createRenderView(1.0F);

    ASSERT_TRUE(view) << view.error().message;
    ASSERT_EQ(view->meshes.size(), 5U);
    ASSERT_EQ(view->meshes.at(0).instances.size(), 2U);
    EXPECT_FLOAT_EQ(view->meshes.at(0).instances.at(0).modelMatrix[3].x, -1.0F);
    EXPECT_FLOAT_EQ(view->meshes.at(0).instances.at(1).modelMatrix[3].x, 1.0F);
    EXPECT_EQ(view->meshes.at(0).instances.at(0).tint, (glm::vec4 {1.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(view->meshes.at(0).instances.at(1).tint, (glm::vec4 {0.0F, 1.0F, 0.0F, 1.0F}));
    EXPECT_EQ(view->meshes.at(1).material.baseColor, (glm::vec4 {0.5F, 0.5F, 0.5F, 1.0F}));
    EXPECT_EQ(view->meshes.at(2).instances.size(), 1U);
    EXPECT_LT(glm::determinant(glm::mat3 {view->meshes.at(2).instances.front().modelMatrix}), 0.0F);
    EXPECT_EQ(view->meshes.at(3).material.alphaMode, panda::AlphaMode::Blend);
    EXPECT_EQ(view->meshes.at(4).material.alphaMode, panda::AlphaMode::Blend);
    EXPECT_EQ(view->meshes.at(3).instances.size(), 1U);
    EXPECT_EQ(view->meshes.at(4).instances.size(), 1U);
}

TEST_F(SceneRenderingSystem, StartsEmptyAndRendersExplicitlyCreatedCameraAndLight)
{
    auto scene = panda::scene::Scene {};

    EXPECT_FALSE(scene.createRenderView(16.0F / 9.0F));

    const auto camera = scene.createEntity({.name = "Camera"});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    const auto unlitView = scene.createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(unlitView);
    EXPECT_EQ(unlitView->camera.position, glm::vec3 {});
    EXPECT_TRUE(unlitView->lights.empty());

    ASSERT_TRUE(scene.setLocalTransform(*camera,
                                        {
                                            .position = {1.0F, 2.0F, 3.0F}
    }));
    const auto editedView = scene.createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(editedView);
    EXPECT_EQ(editedView->camera.position, (glm::vec3 {1.0F, 2.0F, 3.0F}));

    const auto light = scene.createEntity({.name = "Sun"});
    ASSERT_TRUE(light);
    ASSERT_TRUE(scene.setLight(*light, panda::scene::DirectionalLight {}));
    const auto litView = scene.createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(litView);
    ASSERT_EQ(litView->lights.size(), 1U);
    EXPECT_FLOAT_EQ(std::get<panda::DirectionalLight>(litView->lights.front()).intensity, 3.0F);
    ASSERT_TRUE(scene.setLight(*light, panda::scene::DirectionalLight {.intensity = 1.5F, .shadow = {}}));
    const auto editedLightView = scene.createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(editedLightView);
    ASSERT_EQ(editedLightView->lights.size(), 1U);
    EXPECT_FLOAT_EQ(std::get<panda::DirectionalLight>(editedLightView->lights.front()).intensity, 1.5F);
}

TEST(ScenePublicApi, ExercisesObjectIdentityAndOrthographicCameraApi)
{
    auto created = panda::Engine::create({.applicationName = "Panda scene API", .enableValidation = false});
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created) << created.error().message;
    auto scene = panda::scene::Scene {};

    const auto objectId = panda::scene::ObjectId {0xfedcba9876543210ULL};
    const auto object = scene.createEntity(objectId, {.name = "stable"});
    ASSERT_TRUE(object);
    const auto resolvedId = scene.getObjectId(*object);
    ASSERT_TRUE(resolvedId);
    EXPECT_EQ(*resolvedId, objectId);
    const auto foundEntity = scene.findEntity(objectId);
    ASSERT_TRUE(foundEntity.has_value()) << "The explicit ObjectId should resolve while its entity is alive";
    EXPECT_EQ(foundEntity.value(), *object);
    EXPECT_FALSE(scene.findEntity(panda::scene::ObjectId {0}));
    ASSERT_TRUE(scene.destroyEntity(*object));
    EXPECT_FALSE(scene.findEntity(objectId));

    const auto camera = scene.createEntity({.name = "Orthographic camera"});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene.setOrthographicCamera(*camera, panda::scene::OrthographicCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*camera));
    ASSERT_TRUE(scene.createRenderView(1.0F));

    const auto child = scene.createEntity({.name = "Mesh and light"});
    ASSERT_TRUE(child);
    auto mesh = created->getResourceManager().createMeshFromGeometry(panda::test::makeTestTriangleGeometry());
    ASSERT_TRUE(mesh);
    ASSERT_TRUE(scene.setParent(*child, *camera));
    ASSERT_TRUE(scene.setMesh(*child, panda::scene::MeshInstance {.mesh = *mesh}));
    ASSERT_TRUE(scene.setLight(*child, panda::scene::DirectionalLight {}));
    ASSERT_TRUE(scene.destroySubtree(*camera));

    const auto survivor = scene.createEntity();
    ASSERT_TRUE(survivor);
    auto moved = std::move(scene);
    EXPECT_TRUE(moved.isAlive(*survivor));
}

TEST_F(SceneRenderingSystem, RejectsMeshesFromAnotherEngineBeforeSubmission)
{
    auto other = panda::Engine::create();
    ASSERT_TRUE(other);
    auto mesh = other->getResourceManager().createMeshFromGeometry(panda::test::makeTestTriangleGeometry());
    ASSERT_TRUE(mesh);
    auto scene = panda::scene::Scene {};

    selectCamera(scene);
    const auto object = scene.createEntity();
    ASSERT_TRUE(object);
    ASSERT_TRUE(scene.setMesh(*object, panda::scene::MeshInstance {.mesh = *mesh}));
    const auto view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    ASSERT_TRUE(renderTarget.isValid());
    const auto rendered = activeEngine().getOffscreenRenderer().render(*view, renderTarget);
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(scene.setVisible(*object, false));
    EXPECT_TRUE(scene.createRenderView(1.0F));
}

TEST_F(SceneRenderingSystem, RejectsTexturesFromAnotherEngineBeforeSubmission)
{
    auto other = panda::Engine::create();
    ASSERT_TRUE(other);
    auto texture = other->getResourceManager().createTexture({
        .extent = glm::uvec2 {1, 1},
        .format = ::panda::PixelFormat::Rgba8Srgb,
        .usage = ::panda::TextureUsage::TransferDestination | ::panda::TextureUsage::Sampled
    });
    ASSERT_TRUE(texture);

    auto scene = panda::scene::Scene {};

    selectCamera(scene);
    const auto object = scene.createEntity();
    ASSERT_TRUE(object);
    auto mesh = activeEngine().getResourceManager().createMeshFromGeometry(panda::test::makeTestTriangleGeometry());
    ASSERT_TRUE(mesh);
    ASSERT_TRUE(scene.setMesh(*object, panda::scene::MeshInstance {.mesh = *mesh}));
    auto view = scene.createRenderView(1.0F);
    ASSERT_TRUE(view);
    view->meshes.front().material.baseColorTexture = *texture;

    ASSERT_TRUE(renderTarget.isValid());
    const auto rendered = activeEngine().getOffscreenRenderer().render(*view, renderTarget);
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(SceneRenderingSystem, MovingScenePreservesIdentity)
{
    auto scene = panda::scene::Scene {};

    const auto entity = scene.createEntity();
    ASSERT_TRUE(entity);
    const auto entityId = scene.getObjectId(*entity);
    ASSERT_TRUE(entityId);
    EXPECT_EQ(scene.findEntity(*entityId), entity);
    ASSERT_TRUE(scene.setName(*entity, "root"));
    const auto name = scene.getName(*entity);
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "root");
    ASSERT_TRUE(scene.setLocalTransform(*entity,
                                        {
                                            .position = {1.0F, 2.0F, 3.0F}
    }));
    ASSERT_TRUE(scene.getLocalTransform(*entity));
    ASSERT_TRUE(scene.setWorldTransform(*entity,
                                        {
                                            .position = {1.0F, 2.0F, 3.0F}
    }));
    ASSERT_TRUE(scene.getWorldTransform(*entity));
    const auto parent = scene.getParent(*entity);
    ASSERT_TRUE(parent);
    EXPECT_FALSE(*parent);
    ASSERT_TRUE(scene.setParent(*entity, std::nullopt));
    const auto child = scene.createEntity();
    ASSERT_TRUE(child);
    ASSERT_TRUE(scene.setParent(*child, std::optional<panda::scene::Entity> {*entity}));
    const auto childParent = scene.getParent(*child);
    ASSERT_TRUE(childParent);
    ASSERT_TRUE(childParent->has_value());
    EXPECT_EQ(childParent->value_or(panda::scene::Entity {}), *entity);
    ASSERT_TRUE(scene.destroySubtree(*child));
    ASSERT_TRUE(scene.setMesh(*entity, std::nullopt));
    ASSERT_TRUE(scene.setCamera(*entity, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene.setActiveCamera(*entity));
    ASSERT_TRUE(scene.setLight(*entity, std::nullopt));
    ASSERT_TRUE(scene.setVisible(*entity, true));
    ASSERT_TRUE(scene.createRenderView(1.0F));
    EXPECT_EQ(*entity, *entity);
    EXPECT_EQ(panda::scene::Entity {}, panda::scene::Entity {});

    auto otherScene = panda::scene::Scene {};

    const auto otherEntity = otherScene.createEntity();
    ASSERT_TRUE(otherEntity);
    EXPECT_NE(*entity, *otherEntity);
    ASSERT_TRUE(otherScene.destroySubtree(*otherEntity));

    auto moved = std::move(scene);

    auto destination = panda::scene::Scene {};

    destination = std::move(moved);
    EXPECT_TRUE(destination.isAlive(*entity));
    auto& target = destination;
    auto& source = target;
    target = std::move(source);
    EXPECT_TRUE(destination.isAlive(*entity));
}

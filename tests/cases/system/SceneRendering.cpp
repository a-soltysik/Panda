#include <gtest/gtest.h>

#include <optional>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <panda/scene/Entity.hpp>
#include <panda/scene/Scene.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace
{
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
        auto created = panda::Context::create({.applicationName = "Panda headless scene", .enableValidation = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        context.emplace(std::move(*created));
    }

    auto activeContext() -> panda::Context&
    {
        if (!context)
        {
            panda::panic("Scene system test has no initialized Context");
        }
        return *context;
    }

    std::optional<panda::Context> context;
};
}

TEST_F(SceneRenderingSystem, BuildsHeadlessSnapshotAndRetainsDeletedObjectMesh)
{
    auto scene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(scene);
    selectCamera(*scene);
    auto mesh = activeContext().createMeshFromGeometry(panda::createCubeGeometry());
    ASSERT_TRUE(mesh);
    const auto object = scene->createEntity({.name = {}, .transform = {.position = {2.0F, 0.0F, 0.0F}}});
    ASSERT_TRUE(object);
    ASSERT_TRUE(scene->setMesh(*object, panda::scene::MeshInstance {.mesh = *mesh}));
    auto moved = std::move(*scene);
    EXPECT_TRUE(moved.isAlive(*object));
    const auto view = moved.createRenderView(1.5F);
    ASSERT_TRUE(view) << view.error().message;
    ASSERT_EQ(view->meshes.size(), 1);
    EXPECT_EQ(view->meshes.front().modelMatrix[3].x, 2.0F);
    ASSERT_TRUE(moved.destroyEntity(*object));
    mesh->reset();
    EXPECT_TRUE(view->meshes.front().mesh.isValid());
    const auto empty = moved.createRenderView(1.5F);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->meshes.empty());
}

TEST_F(SceneRenderingSystem, StartsEmptyAndRendersExplicitlyCreatedCameraAndLight)
{
    auto scene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(scene);
    EXPECT_FALSE(scene->createRenderView(16.0F / 9.0F));

    const auto camera = scene->createEntity({.name = "Camera"});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene->setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene->setActiveCamera(*camera));
    const auto unlitView = scene->createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(unlitView);
    EXPECT_EQ(unlitView->camera.position, glm::vec3 {});
    EXPECT_TRUE(unlitView->lights.empty());

    ASSERT_TRUE(scene->setLocalTransform(*camera,
                                         {
                                             .position = {1.0F, 2.0F, 3.0F}
    }));
    const auto editedView = scene->createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(editedView);
    EXPECT_EQ(editedView->camera.position, (glm::vec3 {1.0F, 2.0F, 3.0F}));

    const auto light = scene->createEntity({.name = "Sun"});
    ASSERT_TRUE(light);
    ASSERT_TRUE(scene->setLight(*light, panda::scene::DirectionalLight {}));
    const auto litView = scene->createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(litView);
    ASSERT_EQ(litView->lights.size(), 1U);
    EXPECT_FLOAT_EQ(std::get<panda::DirectionalLight>(litView->lights.front()).intensity, 3.0F);
    ASSERT_TRUE(scene->setLight(*light, panda::scene::DirectionalLight {.intensity = 1.5F}));
    const auto editedLightView = scene->createRenderView(16.0F / 9.0F);
    ASSERT_TRUE(editedLightView);
    ASSERT_EQ(editedLightView->lights.size(), 1U);
    EXPECT_FLOAT_EQ(std::get<panda::DirectionalLight>(editedLightView->lights.front()).intensity, 1.5F);
}

TEST(ScenePublicApi, ExercisesObjectIdentityAndOrthographicCameraApi)
{
    auto created = panda::Context::create({.applicationName = "Panda scene API", .enableValidation = false});
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created) << created.error().message;
    auto scene = panda::scene::Scene::create(*created);
    ASSERT_TRUE(scene);
    const auto objectId = panda::scene::ObjectId {0xfedcba9876543210ULL};
    const auto object = scene->createEntity(objectId, {.name = "stable"});
    ASSERT_TRUE(object);
    const auto resolvedId = scene->getObjectId(*object);
    ASSERT_TRUE(resolvedId);
    EXPECT_EQ(*resolvedId, objectId);
    const auto foundEntity = scene->findEntity(objectId);
    if (foundEntity)
    {
        EXPECT_EQ(*foundEntity, *object);
    }
    else
    {
        FAIL() << "The explicit ObjectId should resolve while its entity is alive";
    }
    EXPECT_FALSE(scene->findEntity(panda::scene::ObjectId {0}));
    ASSERT_TRUE(scene->destroyEntity(*object));
    EXPECT_FALSE(scene->findEntity(objectId));

    const auto camera = scene->createEntity({.name = "Orthographic camera"});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene->setOrthographicCamera(*camera, panda::scene::OrthographicCamera {}));
    ASSERT_TRUE(scene->setActiveCamera(*camera));
    ASSERT_TRUE(scene->createRenderView(1.0F));

    const auto child = scene->createEntity({.name = "Mesh and light"});
    ASSERT_TRUE(child);
    auto mesh = created->createMeshFromGeometry(panda::createCubeGeometry());
    ASSERT_TRUE(mesh);
    ASSERT_TRUE(scene->setParent(*child, *camera));
    ASSERT_TRUE(scene->setMesh(*child, panda::scene::MeshInstance {.mesh = *mesh}));
    ASSERT_TRUE(scene->setLight(*child, panda::scene::DirectionalLight {}));
    ASSERT_TRUE(scene->destroySubtree(*camera));

    const auto survivor = scene->createEntity();
    ASSERT_TRUE(survivor);
    auto moved = std::move(*scene);
    EXPECT_TRUE(moved.isAlive(*survivor));
    EXPECT_FALSE(scene->createEntity(panda::scene::ObjectId {99}));
}

TEST_F(SceneRenderingSystem, RejectsMeshesFromAnotherContextBeforeSubmission)
{
    auto other = panda::Context::create();
    ASSERT_TRUE(other);
    auto mesh = other->createMeshFromGeometry(panda::createCubeGeometry());
    ASSERT_TRUE(mesh);
    auto scene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(scene);
    selectCamera(*scene);
    const auto object = scene->createEntity();
    ASSERT_TRUE(object);
    ASSERT_TRUE(scene->setMesh(*object, panda::scene::MeshInstance {.mesh = *mesh}));
    const auto view = scene->createRenderView(1.0F);
    ASSERT_TRUE(view);
    const auto rendered = activeContext().render(*view);
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(scene->setVisible(*object, false));
    EXPECT_TRUE(scene->createRenderView(1.0F));
}

TEST_F(SceneRenderingSystem, RejectsTexturesFromAnotherContextBeforeSubmission)
{
    auto other = panda::Context::create();
    ASSERT_TRUE(other);
    auto texture = other->createTexture({
        .extent = vk::Extent2D {1, 1},
        .format = vk::Format::eR8G8B8A8Srgb,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    });
    ASSERT_TRUE(texture);

    auto scene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(scene);
    selectCamera(*scene);
    const auto object = scene->createEntity();
    ASSERT_TRUE(object);
    auto mesh = activeContext().createMeshFromGeometry(panda::createCubeGeometry());
    ASSERT_TRUE(mesh);
    ASSERT_TRUE(scene->setMesh(*object, panda::scene::MeshInstance {.mesh = *mesh}));
    auto view = scene->createRenderView(1.0F);
    ASSERT_TRUE(view);
    view->meshes.front().material.baseColorTexture = *texture;

    const auto rendered = activeContext().render(*view);
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(SceneRenderingSystem, RejectsMovedFromSceneOperationsAndPreservesMovedIdentity)
{
    auto savedContext = std::move(activeContext());
    const auto invalidContextScene = panda::scene::Scene::create(activeContext());
    context.emplace(std::move(savedContext));
    EXPECT_FALSE(invalidContextScene);

    auto scene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(scene);
    const auto entity = scene->createEntity();
    ASSERT_TRUE(entity);
    const auto entityId = scene->getObjectId(*entity);
    ASSERT_TRUE(entityId);
    EXPECT_EQ(scene->findEntity(*entityId), entity);
    ASSERT_TRUE(scene->setName(*entity, "root"));
    const auto name = scene->getName(*entity);
    ASSERT_TRUE(name);
    EXPECT_EQ(*name, "root");
    ASSERT_TRUE(scene->setLocalTransform(*entity,
                                         {
                                             .position = {1.0F, 2.0F, 3.0F}
    }));
    ASSERT_TRUE(scene->getLocalTransform(*entity));
    ASSERT_TRUE(scene->setWorldTransform(*entity,
                                         {
                                             .position = {1.0F, 2.0F, 3.0F}
    }));
    ASSERT_TRUE(scene->getWorldTransform(*entity));
    const auto parent = scene->getParent(*entity);
    ASSERT_TRUE(parent);
    EXPECT_FALSE(*parent);
    ASSERT_TRUE(scene->setParent(*entity, std::nullopt));
    const auto child = scene->createEntity();
    ASSERT_TRUE(child);
    ASSERT_TRUE(scene->setParent(*child, std::optional<panda::scene::Entity> {*entity}));
    const auto childParent = scene->getParent(*child);
    ASSERT_TRUE(childParent);
    ASSERT_TRUE(childParent->has_value());
    EXPECT_EQ(childParent->value_or(panda::scene::Entity {}), *entity);
    ASSERT_TRUE(scene->destroySubtree(*child));
    ASSERT_TRUE(scene->setMesh(*entity, std::nullopt));
    ASSERT_TRUE(scene->setCamera(*entity, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene->setActiveCamera(*entity));
    ASSERT_TRUE(scene->setLight(*entity, std::nullopt));
    ASSERT_TRUE(scene->setVisible(*entity, true));
    ASSERT_TRUE(scene->createRenderView(1.0F));
    EXPECT_EQ(*entity, *entity);
    EXPECT_EQ(panda::scene::Entity {}, panda::scene::Entity {});

    auto otherScene = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(otherScene);
    const auto otherEntity = otherScene->createEntity();
    ASSERT_TRUE(otherEntity);
    EXPECT_NE(*entity, *otherEntity);
    ASSERT_TRUE(otherScene->destroySubtree(*otherEntity));

    auto moved = std::move(*scene);
    EXPECT_FALSE(scene->createEntity());
    EXPECT_FALSE(scene->isAlive(*entity));
    EXPECT_FALSE(scene->getObjectId(*entity));
    EXPECT_FALSE(scene->findEntity(*entityId));
    EXPECT_FALSE(scene->getLocalTransform(*entity));
    EXPECT_FALSE(scene->getName(*entity));
    EXPECT_FALSE(scene->setName(*entity, "ignored"));
    EXPECT_FALSE(scene->setLocalTransform(*entity, {}));
    EXPECT_FALSE(scene->getWorldTransform(*entity));
    EXPECT_FALSE(scene->setWorldTransform(*entity, {}));
    EXPECT_FALSE(scene->getParent(*entity));
    EXPECT_FALSE(scene->setParent(*entity, std::nullopt));
    EXPECT_FALSE(scene->destroyEntity(*entity));
    EXPECT_FALSE(scene->destroySubtree(*entity));
    EXPECT_FALSE(scene->setMesh(*entity, std::nullopt));
    EXPECT_FALSE(scene->setCamera(*entity, std::nullopt));
    EXPECT_FALSE(scene->setOrthographicCamera(*entity, std::nullopt));
    EXPECT_FALSE(scene->setLight(*entity, std::nullopt));
    EXPECT_FALSE(scene->setVisible(*entity, true));
    EXPECT_FALSE(scene->setActiveCamera(*entity));
    EXPECT_FALSE(scene->createRenderView(1.0F));

    auto destination = panda::scene::Scene::create(activeContext());
    ASSERT_TRUE(destination);
    *destination = std::move(moved);
    EXPECT_TRUE(destination->isAlive(*entity));
    auto& target = *destination;
    auto& source = target;
    target = std::move(source);
    EXPECT_TRUE(destination->isAlive(*entity));
}

#include <gtest/gtest.h>

#include <optional>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/scene/Scene.hpp>
#include <utility>

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
    ASSERT_TRUE(scene->setDirectionalLight(*entity, std::nullopt));
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
    EXPECT_FALSE(scene->setDirectionalLight(*entity, std::nullopt));
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

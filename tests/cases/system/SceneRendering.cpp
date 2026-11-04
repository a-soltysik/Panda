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

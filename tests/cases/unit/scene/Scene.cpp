#include <gtest/gtest.h>

#include <optional>
#include <panda/base/Error.hpp>
#include <panda/scene/Scene.hpp>
#include <tuple>
#include <utility>

TEST(Scene, ConstructsAndPreparesRenderView)
{
    auto scene = std::optional<panda::scene::Scene> {std::in_place};

    const auto missingCamera = scene->createRenderView(1.0F);
    ASSERT_FALSE(missingCamera);
    EXPECT_EQ(missingCamera.error().code, panda::ErrorCode::InvalidArgument);

    const auto camera = scene->createEntity({.name = "Camera"});
    ASSERT_TRUE(camera);
    ASSERT_TRUE(scene->setCamera(*camera, panda::scene::PerspectiveCamera {}));
    ASSERT_TRUE(scene->setActiveCamera(*camera));
    EXPECT_FALSE(scene->createRenderView(0.0F));

    const auto view = scene->createRenderView(1.0F);
    ASSERT_TRUE(view);
    EXPECT_TRUE(view->meshes.empty());
    EXPECT_TRUE(view->lights.empty());

    auto moved = std::move(*scene);
    EXPECT_DEATH(std::ignore = scene->createEntity(), "Cannot use a moved-from Scene");
    EXPECT_DEATH(std::ignore = scene->createRenderView(1.0F), "Cannot use a moved-from Scene");
    EXPECT_TRUE(moved.isAlive(*camera));
    EXPECT_TRUE(moved.createRenderView(1.0F));
}

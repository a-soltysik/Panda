// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "renderer/RenderViewValidation.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <panda/Error.hpp>
#include <panda/Projection.hpp>
#include <panda/RenderView.hpp>

namespace
{
using panda::ErrorCode;
using panda::RenderView;
using panda::core::vulkan::renderer::validateRenderViewData;
}

TEST(RenderViewValidation, AcceptsEmptyBackgroundView)
{
    EXPECT_TRUE(validateRenderViewData(RenderView {}));
}

TEST(RenderViewValidation, RejectsNonFiniteCameraAndZeroDirection)
{
    auto view = RenderView {};
    view.camera.position.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.camera.viewProjection = panda::Mat4 {0.0F};
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.direction = {};
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, AcceptsLargeOrthographicProjection)
{
    const auto projection = panda::createOrthographicProjection(1000.0F, 1.0F, 0.1F, 1000.0F);
    ASSERT_TRUE(projection);
    auto view = RenderView {};
    view.camera.viewProjection = *projection;
    EXPECT_TRUE(validateRenderViewData(view));
}

TEST(RenderViewValidation, RejectsSingularModelAndNegativeTint)
{
    auto view = RenderView {};
    view.meshes.push_back({.mesh = {}, .modelMatrix = panda::Mat4 {0.0F}});
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.clear();
    view.meshes.push_back({
        .mesh = {},
        .tint = {-1.0F, 1.0F, 1.0F, 1.0F}
    });
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

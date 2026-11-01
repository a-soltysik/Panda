// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "renderer/RenderViewValidation.hpp"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
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
    view.camera.viewProjection[0][0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.camera.viewProjection = glm::mat4 {0.0F};
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.direction = {};
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, RejectsInvalidDirectionalLight)
{
    auto view = RenderView {};
    view.directionalLight.direction = glm::vec3 {std::numeric_limits<float>::max()};
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.direction.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.color.r = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.intensity = -1.0F;
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view = RenderView {};
    view.directionalLight.color.g = -1.0F;
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
    view.meshes.push_back({.mesh = {}, .modelMatrix = glm::mat4 {0.0F}});
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.clear();
    view.meshes.push_back({
        .mesh = {},
        .tint = {-1.0F, 1.0F, 1.0F, 1.0F}
    });
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, RejectsNonAffineAndNonInvertibleModels)
{
    auto view = RenderView {};
    auto model = glm::mat4 {1.0F};
    model[0][3] = 1.0F;
    view.meshes.push_back({.mesh = {}, .modelMatrix = model});
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.clear();
    model = glm::mat4 {1.0F};
    model[0][0] = 1.0e-40F;
    view.meshes.push_back({.mesh = {}, .modelMatrix = model});
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.clear();
    view.meshes.push_back({
        .mesh = {},
        .tint = {1.0F, 1.0F, 1.0F, -1.0F}
    });
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

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

TEST(RenderViewValidation, RejectsSingularModel)
{
    auto view = RenderView {};
    view.meshes.push_back({.mesh = {}, .modelMatrix = glm::mat4 {0.0F}});
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, RejectsEachNegativeTintChannelAndNonFiniteTint)
{
    auto view = RenderView {};
    view.meshes.push_back({});
    auto& tint = view.meshes.back().tint;
    const auto rejectNegative = [&view](float& component) {
        component = -1.0F;
        EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
        component = 1.0F;
    };
    rejectNegative(tint.r);
    rejectNegative(tint.g);
    rejectNegative(tint.b);
    rejectNegative(tint.a);

    tint.r = std::numeric_limits<float>::infinity();
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
}

TEST(RenderViewValidation, RejectsNonFiniteMaterialValuesAndUnknownModel)
{
    auto view = RenderView {};
    view.meshes.push_back({});
    view.meshes.back().material.metallic = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.back().material.metallic = 0.0F;
    view.meshes.back().material.baseColor.g = std::numeric_limits<float>::infinity();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.back().material.baseColor = glm::vec4 {1.0F};
    // Exercise rejection of an out-of-range enum supplied by malformed application data.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    view.meshes.back().material.model = static_cast<panda::MaterialModel>(255);
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    view.meshes.back().material.model = panda::MaterialModel::Lit;
    view.meshes.back().material.renderFace = panda::RenderFace::Both;
    EXPECT_TRUE(validateRenderViewData(view));

    // Exercise rejection of an out-of-range enum supplied by malformed application data.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    view.meshes.back().material.renderFace = static_cast<panda::RenderFace>(255);
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, RejectsEachNegativeMaterialColorChannelAndNonFiniteRoughness)
{
    auto view = RenderView {};
    view.meshes.push_back({});
    auto& color = view.meshes.back().material.baseColor;
    const auto rejectNegative = [&view](float& component) {
        component = -1.0F;
        EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
        component = 1.0F;
    };
    rejectNegative(color.r);
    rejectNegative(color.g);
    rejectNegative(color.b);
    rejectNegative(color.a);

    view.meshes.back().material.roughness = std::numeric_limits<float>::infinity();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

TEST(RenderViewValidation, ValidatesAlphaModeAndCutoff)
{
    auto view = RenderView {};
    view.meshes.push_back({});
    auto& material = view.meshes.back().material;
    EXPECT_TRUE(validateRenderViewData(view));

    material.alphaMode = panda::AlphaMode::Mask;
    material.alphaCutoff = 0.0F;
    EXPECT_TRUE(validateRenderViewData(view));
    material.alphaMode = panda::AlphaMode::Blend;
    EXPECT_TRUE(validateRenderViewData(view));

    material.alphaMode = panda::AlphaMode::Mask;
    material.alphaCutoff = 1.0F;
    EXPECT_TRUE(validateRenderViewData(view));

    material.alphaCutoff = -0.01F;
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
    material.alphaCutoff = 1.01F;
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
    material.alphaCutoff = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
    material.alphaCutoff = std::numeric_limits<float>::infinity();
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);

    // Exercise rejection of an out-of-range enum supplied by malformed application data.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    material.alphaMode = static_cast<panda::AlphaMode>(255);
    EXPECT_EQ(validateRenderViewData(view).error().code, ErrorCode::InvalidArgument);
}

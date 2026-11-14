#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float4.hpp>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Camera.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/Geometry.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "LogCapture.hpp"
#include "resources/OffscreenTargetResource.hpp"

namespace
{
auto createBlendQuad(const panda::Context& context) -> panda::Result<panda::Mesh>
{
    auto geometry = panda::Geometry {};
    geometry.vertices = {{.position = {-0.8F, -0.8F, 0.0F}},
                         {.position = {0.8F, -0.8F, 0.0F}},
                         {.position = {0.8F, 0.8F, 0.0F}},
                         {.position = {-0.8F, -0.8F, 0.0F}},
                         {.position = {0.8F, 0.8F, 0.0F}},
                         {.position = {-0.8F, 0.8F, 0.0F}}};
    auto mesh = context.getResourceManager().createMeshFromGeometry(geometry);
    if (!mesh)
    {
        return std::unexpected {mesh.error()};
    }
    auto batch = context.getSubmissionManager().createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = batch->write(*mesh, geometry); !written)
    {
        return std::unexpected {written.error()};
    }
    const auto completion = batch->submit();
    if (!completion)
    {
        return std::unexpected {completion.error()};
    }
    const auto waited = context.getSubmissionManager().wait(*completion, std::chrono::seconds {10});
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (*waited != panda::WaitStatus::Completed)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "Blend quad upload timed out")};
    }
    return mesh;
}

auto makeBlendDraw(const panda::Mesh& mesh, float z, glm::vec4 color) -> panda::MeshDraw
{
    auto transform = glm::mat4 {1.0F};
    transform[3].z = z;
    auto draw = panda::MeshDraw {.mesh = mesh, .modelMatrix = transform};
    draw.material.model = panda::MaterialModel::Unlit;
    draw.material.baseColor = color;
    draw.material.renderFace = panda::RenderFace::Both;
    draw.material.alphaMode = panda::AlphaMode::Blend;
    return draw;
}

auto makeBlendView(const panda::Mesh& mesh, bool reverse) -> panda::Result<panda::RenderView>
{
    const auto camera = panda::createPerspectiveCamera({
        .position = {0.0F, 0.0F, 3.0F},
        .target = {},
        .up = {0.0F, 1.0F, 0.0F},
        .verticalFieldOfViewRadians = 0.7853982F,
        .aspectRatio = 1.0F,
        .nearPlane = 0.1F,
        .farPlane = 20.0F
    });
    if (!camera)
    {
        return std::unexpected {camera.error()};
    }
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}};
    const auto red = makeBlendDraw(mesh, 0.0F, {0.8F, 0.0F, 0.0F, 0.5F});
    const auto blue = makeBlendDraw(mesh, 0.25F, {0.0F, 0.0F, 0.7F, 0.5F});
    view.meshes = reverse ? std::vector<panda::MeshDraw> {blue, red} : std::vector<panda::MeshDraw> {red, blue};
    return view;
}

auto toneMap(float value) -> float
{
    return std::clamp((value * ((2.51F * value) + 0.03F)) / ((value * ((2.43F * value) + 0.59F)) + 0.14F), 0.0F, 1.0F);
}

auto encodeSrgb(float value) -> float
{
    return value <= 0.0031308F ? value * 12.92F : (1.055F * std::pow(value, 1.0F / 2.4F)) - 0.055F;
}

auto toByte(float value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

auto expectedCenterPixel() -> std::array<std::uint8_t, 4>
{
    constexpr auto backgroundRed = 0.025F;
    constexpr auto backgroundGreen = 0.035F;
    constexpr auto backgroundBlue = 0.05F;
    const auto red = toneMap(0.8F);
    const auto blue = toneMap(0.7F);
    return {toByte(encodeSrgb((0.25F * red) + (0.25F * backgroundRed))),
            toByte(encodeSrgb(0.25F * backgroundGreen)),
            toByte(encodeSrgb((0.5F * blue) + (0.25F * backgroundBlue))),
            255};
}

auto centerPixel(const panda::ImageReadback& image) -> std::array<std::uint8_t, 4>
{
    const auto offset =
        ((static_cast<std::size_t>(image.extent.height / 2) * image.extent.width) + (image.extent.width / 2)) * 4;
    return {image.rgba.at(offset), image.rgba.at(offset + 1), image.rgba.at(offset + 2), image.rgba.at(offset + 3)};
}

class OffscreenRenderingSystem : public testing::Test
{
protected:
    void SetUp() override
    {
        auto created = panda::Context::create({.applicationName = "Panda offscreen rendering",
                                               .enableValidation = true,
                                               .enableFramePerformanceSampling = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        context.emplace(std::move(*created));
    }

    void TearDown() override
    {
        context.reset();
        const auto error = std::ranges::find_if(records.entries, [](const auto& entry) {
            return entry.level == panda::log::Level::Error;
        });
        EXPECT_EQ(error, records.entries.end()) << (error == records.entries.end() ? "" : error->message);
    }

    auto activeContext() -> panda::Context&
    {
        if (!context)
        {
            panda::panic("Offscreen system test has no initialized Context");
        }
        return *context;
    }

    panda::test::LogRecords records;
    panda::test::ProcessSinkRegistration capture {records};
    std::optional<panda::Context> context;
};
}

TEST_F(OffscreenRenderingSystem, ReadsBlendedPixelsIndependentOfInputDrawOrder)
{
    auto createdTarget = activeContext().getResourceManager().createOffscreenTarget({
        .extent = {.width = 32, .height = 32}
    });
    if (!createdTarget && createdTarget.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << createdTarget.error().message;
    }
    ASSERT_TRUE(createdTarget) << createdTarget.error().message;
    auto target = std::move(*createdTarget);
    auto mesh = createBlendQuad(activeContext());
    if (!mesh && mesh.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << mesh.error().message;
    }
    ASSERT_TRUE(mesh) << mesh.error().message;
    const auto firstView = makeBlendView(*mesh, true);
    ASSERT_TRUE(firstView) << firstView.error().message;
    const auto firstFrame = activeContext().getOffscreenRenderer().render(*firstView, target);
    if (!firstFrame && firstFrame.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << firstFrame.error().message;
    }
    ASSERT_TRUE(firstFrame) << firstFrame.error().message;
    EXPECT_EQ(firstFrame->status, panda::FrameResult::Status::Rendered);
    EXPECT_TRUE(firstFrame->completion.isValid());
    const auto firstReadback = activeContext().getOffscreenRenderer().readPixels(target, firstFrame->completion);
    ASSERT_TRUE(firstReadback) << firstReadback.error().message;

    const auto secondView = makeBlendView(*mesh, false);
    ASSERT_TRUE(secondView) << secondView.error().message;
    const auto secondFrame = activeContext().getOffscreenRenderer().render(*secondView, target);
    ASSERT_TRUE(secondFrame) << secondFrame.error().message;
    const auto staleReadback = activeContext().getOffscreenRenderer().readPixels(target, firstFrame->completion);
    ASSERT_FALSE(staleReadback);
    EXPECT_EQ(staleReadback.error().code, panda::ErrorCode::InvalidArgument);
    const auto secondReadback = activeContext().getOffscreenRenderer().readPixels(target, secondFrame->completion);
    ASSERT_TRUE(secondReadback) << secondReadback.error().message;
    ASSERT_EQ(firstReadback->rgba.size(), 32U * 32U * 4U);
    ASSERT_EQ(secondReadback->rgba.size(), firstReadback->rgba.size());

    const auto expected = expectedCenterPixel();
    const auto first = centerPixel(*firstReadback);
    const auto second = centerPixel(*secondReadback);
    for (auto channel = std::size_t {}; channel < expected.size(); ++channel)
    {
        EXPECT_NEAR(first.at(channel), expected.at(channel), 8);
        EXPECT_NEAR(second.at(channel), expected.at(channel), 8);
        EXPECT_NEAR(first.at(channel), second.at(channel), 2);
    }
}

TEST_F(OffscreenRenderingSystem, ValidatesTargetExtentOwnershipAndLifetime)
{
    const auto zeroExtent = activeContext().getResourceManager().createOffscreenTarget({
        .extent = {.width = 0, .height = 32}
    });
    ASSERT_FALSE(zeroExtent);
    EXPECT_EQ(zeroExtent.error().code, panda::ErrorCode::InvalidArgument);

    auto other = panda::Context::create();
    ASSERT_TRUE(other);
    auto foreignTarget = other->getResourceManager().createOffscreenTarget({
        .extent = {.width = 16, .height = 16}
    });
    if (!foreignTarget && foreignTarget.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << foreignTarget.error().message;
    }
    ASSERT_TRUE(foreignTarget) << foreignTarget.error().message;
    const auto foreignRender = activeContext().getOffscreenRenderer().render(panda::RenderView {}, *foreignTarget);
    ASSERT_FALSE(foreignRender);
    EXPECT_EQ(foreignRender.error().code, panda::ErrorCode::InvalidArgument);

    auto target = activeContext().getResourceManager().createOffscreenTarget({
        .extent = {.width = 16, .height = 16}
    });
    ASSERT_TRUE(target) << target.error().message;
    EXPECT_EQ(target->getFormat(), vk::Format::eR8G8B8A8Srgb);
    const auto weak = std::weak_ptr {panda::detail::OffscreenTargetResource::getResource(*target)};
    const auto frame = activeContext().getOffscreenRenderer().render(panda::RenderView {}, *target);
    ASSERT_TRUE(frame) << frame.error().message;
    target->reset();
    EXPECT_FALSE(weak.expired());
    const auto waited = activeContext().getSubmissionManager().wait(frame->completion, std::chrono::seconds {10});
    ASSERT_TRUE(waited) << waited.error().message;
    EXPECT_EQ(*waited, panda::WaitStatus::Completed);
    context.reset();
    EXPECT_TRUE(weak.expired());
}

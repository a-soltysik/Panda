#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <memory>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <utility>
#include <vector>

#include "LogCapture.hpp"
#include "resources/OffscreenTargetResource.hpp"

namespace
{
auto createBlendQuad(const panda::Engine& context) -> panda::Result<panda::Mesh>
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
    view.meshes = reverse ? std::vector {blue, red} : std::vector {red, blue};
    return view;
}

auto createTexturedQuad(const panda::Engine& context, glm::vec2 uvMin = {0.0F, 0.0F}, glm::vec2 uvMax = {64.0F, 64.0F})
    -> panda::Result<panda::Mesh>
{
    auto geometry = panda::Geometry {
        .vertices = {{.position = {-0.8F, -0.8F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = uvMin},
                     {.position = {0.8F, -0.8F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = {uvMax.x, uvMin.y}},
                     {.position = {0.8F, 0.8F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = uvMax},
                     {.position = {-0.8F, 0.8F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .uv = {uvMin.x, uvMax.y}}},
        .indices = std::vector<std::uint16_t> {0, 1, 2, 0, 2, 3}
    };
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
        return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "Textured quad upload timed out")};
    }
    return mesh;
}

auto makeTexturedView(const panda::Mesh& mesh, const panda::Texture& texture) -> panda::Result<panda::RenderView>
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
    auto draw = panda::MeshDraw {.mesh = mesh};
    draw.material.model = panda::MaterialModel::Unlit;
    draw.material.renderFace = panda::RenderFace::Both;
    draw.material.baseColorTexture = texture;
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}};
    view.requestedSampleCount = panda::SampleCount::One;
    view.meshes.push_back(std::move(draw));
    return view;
}

auto solidTextureBytes(glm::uvec2 extent, std::array<std::uint8_t, 4> color) -> std::vector<std::byte>
{
    auto bytes = std::vector<std::byte>(static_cast<std::size_t>(extent.x) * extent.y * 4U);
    for (auto index = std::size_t {}; index < bytes.size(); index += 4U)
    {
        for (auto channel = std::size_t {}; channel < color.size(); ++channel)
        {
            bytes.at(index + channel) = static_cast<std::byte>(color.at(channel));
        }
    }
    return bytes;
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

auto pixelAt(const panda::ImageReadback& image, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4>
{
    const auto offset = ((static_cast<std::size_t>(y) * image.extent.x) + x) * 4U;
    return {image.rgba.at(offset), image.rgba.at(offset + 1), image.rgba.at(offset + 2), image.rgba.at(offset + 3)};
}

auto centerPixel(const panda::ImageReadback& image) -> std::array<std::uint8_t, 4>
{
    return pixelAt(image, image.extent.x / 2, image.extent.y / 2);
}

class OffscreenRenderBackendSystem : public testing::Test
{
protected:
    void SetUp() override
    {
        auto created = panda::Engine::create({.applicationName = "Panda offscreen rendering",
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

    auto activeEngine() -> panda::Engine&
    {
        return panda::expect(context, "Offscreen system test has no initialized Engine");
    }

    panda::test::LogRecords records;
    panda::test::ProcessSinkRegistration capture {records};
    std::optional<panda::Engine> context;
};
}

TEST_F(OffscreenRenderBackendSystem, ReadsBlendedPixelsIndependentOfInputDrawOrder)
{
    auto createdTarget = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {32, 32}
    });
    if (!createdTarget && createdTarget.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << createdTarget.error().message;
    }
    ASSERT_TRUE(createdTarget) << createdTarget.error().message;
    auto target = std::move(*createdTarget);
    auto mesh = createBlendQuad(activeEngine());
    if (!mesh && mesh.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << mesh.error().message;
    }
    ASSERT_TRUE(mesh) << mesh.error().message;
    const auto firstView = makeBlendView(*mesh, true);
    ASSERT_TRUE(firstView) << firstView.error().message;
    const auto firstFrame = activeEngine().getOffscreenRenderer().render(*firstView, target);
    if (!firstFrame && firstFrame.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << firstFrame.error().message;
    }
    ASSERT_TRUE(firstFrame) << firstFrame.error().message;
    EXPECT_EQ(firstFrame->status, panda::FrameResult::Status::Rendered);
    EXPECT_TRUE(firstFrame->completion.isValid());
    const auto firstReadback = activeEngine().getOffscreenRenderer().readPixels(target, firstFrame->completion);
    ASSERT_TRUE(firstReadback) << firstReadback.error().message;
    if (activeEngine().getFeatures().performance.isGpuTimestampSamplingEnabled)
    {
        const auto sample = activeEngine().getPerformanceManager().getLatestSample();
        ASSERT_TRUE(sample) << sample.error().message;
        const auto& latest = *sample;
        ASSERT_TRUE(latest);
        EXPECT_EQ(latest.value().frameIndex, 0U);
    }

    const auto secondView = makeBlendView(*mesh, false);
    ASSERT_TRUE(secondView) << secondView.error().message;
    const auto secondFrame = activeEngine().getOffscreenRenderer().render(*secondView, target);
    ASSERT_TRUE(secondFrame) << secondFrame.error().message;
    const auto staleReadback = activeEngine().getOffscreenRenderer().readPixels(target, firstFrame->completion);
    ASSERT_FALSE(staleReadback);
    EXPECT_EQ(staleReadback.error().code, panda::ErrorCode::InvalidArgument);
    const auto secondReadback = activeEngine().getOffscreenRenderer().readPixels(target, secondFrame->completion);
    ASSERT_TRUE(secondReadback) << secondReadback.error().message;
    ASSERT_EQ(firstReadback->rgba.size(), 32U * 32U * 4U);
    ASSERT_EQ(secondReadback->rgba.size(), firstReadback->rgba.size());

    auto smallerTarget = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {16, 24}
    });
    ASSERT_TRUE(smallerTarget) << smallerTarget.error().message;
    const auto resizedFrame = activeEngine().getOffscreenRenderer().render(*secondView, *smallerTarget);
    ASSERT_TRUE(resizedFrame) << resizedFrame.error().message;
    const auto resizedReadback =
        activeEngine().getOffscreenRenderer().readPixels(*smallerTarget, resizedFrame->completion);
    ASSERT_TRUE(resizedReadback) << resizedReadback.error().message;
    const auto smallerExtent = glm::uvec2 {16, 24};
    EXPECT_EQ(resizedReadback->extent, smallerExtent);
    EXPECT_EQ(resizedReadback->rgba.size(), 16U * 24U * 4U);
    if (activeEngine().getFeatures().performance.isGpuTimestampSamplingEnabled)
    {
        auto& performance = activeEngine().getPerformanceManager();
        const auto samples = performance.takeAvailableSamples();
        ASSERT_TRUE(samples) << samples.error().message;
        EXPECT_EQ(samples->size(), 3U);
    }

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

TEST_F(OffscreenRenderBackendSystem, AppliesRepeatMirroredRepeatAndClampToOutOfRangeUvs)
{
    ASSERT_TRUE(activeEngine().setTextureFilteringPreset(panda::TextureFilteringPreset::Nearest));
    const auto target = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {32, 32}
    });
    if (!target && target.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << target.error().message;
    }
    ASSERT_TRUE(target) << target.error().message;
    const auto mesh = createTexturedQuad(activeEngine(), {1.125F, 0.5F}, {2.125F, 0.5F});
    ASSERT_TRUE(mesh) << mesh.error().message;
    const auto stripes = std::array {std::byte {0xff},
                                     std::byte {0x00},
                                     std::byte {0x00},
                                     std::byte {0xff},
                                     std::byte {0x00},
                                     std::byte {0x00},
                                     std::byte {0xff},
                                     std::byte {0xff}};

    for (const auto mode : std::array {panda::TextureAddressMode::Repeat,
                                       panda::TextureAddressMode::MirroredRepeat,
                                       panda::TextureAddressMode::ClampToEdge})
    {
        const auto texture = activeEngine().getResourceManager().createTexture({
            .extent = {2, 1},
            .format = panda::PixelFormat::Rgba8Srgb,
            .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
            .addressMode = mode
        });
        if (!texture && texture.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << texture.error().message;
        }
        ASSERT_TRUE(texture) << texture.error().message;
        auto upload = activeEngine().getSubmissionManager().createUploadBatch();
        ASSERT_TRUE(upload) << upload.error().message;
        ASSERT_TRUE(upload->write(*texture, stripes));
        const auto uploaded = upload->submit();
        ASSERT_TRUE(uploaded) << uploaded.error().message;
        const auto view = makeTexturedView(*mesh, *texture);
        ASSERT_TRUE(view) << view.error().message;
        const auto frame = activeEngine().getOffscreenRenderer().render(*view, *target);
        ASSERT_TRUE(frame) << frame.error().message;
        const auto readback = activeEngine().getOffscreenRenderer().readPixels(*target, frame->completion);
        ASSERT_TRUE(readback) << readback.error().message;

        const auto left = pixelAt(*readback, 8, 16);
        const auto right = pixelAt(*readback, 24, 16);
        switch (mode)
        {
        case panda::TextureAddressMode::Repeat:
            EXPECT_GT(left.at(0), left.at(2));
            EXPECT_GT(right.at(0), right.at(2));
            break;
        case panda::TextureAddressMode::MirroredRepeat:
            EXPECT_GT(left.at(2), left.at(0));
            EXPECT_GT(right.at(0), right.at(2));
            break;
        case panda::TextureAddressMode::ClampToEdge:
            EXPECT_GT(left.at(2), left.at(0));
            EXPECT_GT(right.at(2), right.at(0));
            break;
        }
    }
}

TEST_F(OffscreenRenderBackendSystem, SelectsSuppliedMipsAndSamplesGeneratedSrgbMipsWhenMinifying)
{
    const auto generatedTarget = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {32, 32}
    });
    const auto suppliedTarget = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {32, 32}
    });
    if ((!generatedTarget && generatedTarget.error().code == panda::ErrorCode::Unsupported) ||
        (!suppliedTarget && suppliedTarget.error().code == panda::ErrorCode::Unsupported))
    {
        GTEST_SKIP() << "RGBA8 sRGB offscreen rendering is unavailable";
    }
    ASSERT_TRUE(generatedTarget) << generatedTarget.error().message;
    ASSERT_TRUE(suppliedTarget) << suppliedTarget.error().message;
    const auto mesh = createTexturedQuad(activeEngine());
    if (!mesh && mesh.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << mesh.error().message;
    }
    ASSERT_TRUE(mesh) << mesh.error().message;

    auto generated = activeEngine().getResourceManager().createTexture({
        .extent = {2, 2},
        .format = panda::PixelFormat::Rgba8Srgb,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Generate
    });
    if (!generated && generated.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << generated.error().message;
    }
    ASSERT_TRUE(generated) << generated.error().message;
    const auto checker = std::vector<std::byte> {std::byte {0},
                                                 std::byte {0},
                                                 std::byte {0},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {255},
                                                 std::byte {0},
                                                 std::byte {0},
                                                 std::byte {0},
                                                 std::byte {255}};
    auto generatedUpload = activeEngine().getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(generatedUpload) << generatedUpload.error().message;
    ASSERT_TRUE(generatedUpload->write(*generated, checker));
    const auto generatedUploadCompletion = generatedUpload->submit();
    ASSERT_TRUE(generatedUploadCompletion) << generatedUploadCompletion.error().message;
    auto generatedView = makeTexturedView(*mesh, *generated);
    ASSERT_TRUE(generatedView) << generatedView.error().message;
    const auto generatedFrame = activeEngine().getOffscreenRenderer().render(*generatedView, *generatedTarget);
    ASSERT_TRUE(generatedFrame) << generatedFrame.error().message;
    EXPECT_LE(generatedFrame->effectiveAnisotropy, 8.0F);
    EXPECT_GE(generatedFrame->effectiveAnisotropy, 1.0F);

    ASSERT_TRUE(activeEngine().setTextureFilteringPreset(panda::TextureFilteringPreset::Nearest));
    auto supplied = activeEngine().getResourceManager().createTexture({
        .extent = {4, 4},
        .format = panda::PixelFormat::Rgba8Srgb,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Supplied,
        .addressMode = panda::TextureAddressMode::MirroredRepeat
    });
    if (!supplied && supplied.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << supplied.error().message;
    }
    ASSERT_TRUE(supplied) << supplied.error().message;
    EXPECT_EQ(supplied->getMipLevels(), 3U);
    const auto red = solidTextureBytes({4, 4}, {255, 0, 0, 255});
    const auto green = solidTextureBytes({2, 2}, {0, 255, 0, 255});
    const auto blue = solidTextureBytes({1, 1}, {0, 0, 255, 255});
    auto suppliedUpload = activeEngine().getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(suppliedUpload) << suppliedUpload.error().message;
    ASSERT_TRUE(suppliedUpload->write(*supplied, red));
    ASSERT_TRUE(suppliedUpload->write(*supplied,
                                      green,
                                      {
                                          .extent = {2, 2},
                                          .mipLevel = 1
    }));
    ASSERT_TRUE(suppliedUpload->write(*supplied,
                                      blue,
                                      {
                                          .extent = {1, 1},
                                          .mipLevel = 2
    }));
    const auto suppliedUploadCompletion = suppliedUpload->submit();
    ASSERT_TRUE(suppliedUploadCompletion) << suppliedUploadCompletion.error().message;
    auto suppliedView = makeTexturedView(*mesh, *supplied);
    ASSERT_TRUE(suppliedView) << suppliedView.error().message;
    const auto suppliedFrame = activeEngine().getOffscreenRenderer().render(*suppliedView, *suppliedTarget);
    ASSERT_TRUE(suppliedFrame) << suppliedFrame.error().message;
    EXPECT_FLOAT_EQ(suppliedFrame->effectiveAnisotropy, 1.0F);

    const auto generatedReadback =
        activeEngine().getOffscreenRenderer().readPixels(*generatedTarget, generatedFrame->completion);
    ASSERT_TRUE(generatedReadback) << generatedReadback.error().message;
    const auto suppliedReadback =
        activeEngine().getOffscreenRenderer().readPixels(*suppliedTarget, suppliedFrame->completion);
    ASSERT_TRUE(suppliedReadback) << suppliedReadback.error().message;
    const auto sampledMipLinear = std::pow(((188.0F / 255.0F) + 0.055F) / 1.055F, 2.4F);
    const auto expectedGray = toByte(encodeSrgb(toneMap(sampledMipLinear)));
    const auto generatedPixel = centerPixel(*generatedReadback);
    EXPECT_NEAR(generatedPixel.at(0), expectedGray, 10);
    EXPECT_NEAR(generatedPixel.at(1), expectedGray, 10);
    EXPECT_NEAR(generatedPixel.at(2), expectedGray, 10);
    EXPECT_EQ(generatedPixel.at(3), 255U);

    const auto suppliedPixel = centerPixel(*suppliedReadback);
    EXPECT_LE(suppliedPixel.at(0), 5U);
    EXPECT_LE(suppliedPixel.at(1), 5U);
    EXPECT_NEAR(suppliedPixel.at(2), toByte(encodeSrgb(toneMap(1.0F))), 10);
    EXPECT_EQ(suppliedPixel.at(3), 255U);
}

TEST_F(OffscreenRenderBackendSystem, ValidatesTargetExtentOwnershipAndLifetime)
{
    const auto zeroExtent = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {0, 32}
    });
    ASSERT_FALSE(zeroExtent);
    EXPECT_EQ(zeroExtent.error().code, panda::ErrorCode::InvalidArgument);

    auto emptyTarget = panda::OffscreenTarget {};
    EXPECT_FALSE(emptyTarget.isValid());
    EXPECT_EQ(emptyTarget.getExtent(), glm::uvec2 {});
    EXPECT_FALSE(emptyTarget.isValid());
    const auto emptyReadback = activeEngine().getOffscreenRenderer().readPixels(emptyTarget, {});
    ASSERT_FALSE(emptyReadback);
    EXPECT_EQ(emptyReadback.error().code, panda::ErrorCode::InvalidArgument);

    auto other = panda::Engine::create();
    ASSERT_TRUE(other);
    auto foreignTarget = other->getResourceManager().createOffscreenTarget({
        .extent = {16, 16}
    });
    if (!foreignTarget && foreignTarget.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << foreignTarget.error().message;
    }
    ASSERT_TRUE(foreignTarget) << foreignTarget.error().message;
    const auto foreignRender = activeEngine().getOffscreenRenderer().render(panda::RenderView {}, *foreignTarget);
    ASSERT_FALSE(foreignRender);
    EXPECT_EQ(foreignRender.error().code, panda::ErrorCode::InvalidArgument);
    const auto foreignReadback = activeEngine().getOffscreenRenderer().readPixels(*foreignTarget, {});
    ASSERT_FALSE(foreignReadback);
    EXPECT_EQ(foreignReadback.error().code, panda::ErrorCode::InvalidArgument);
    const auto foreignFrame = other->getOffscreenRenderer().render(panda::RenderView {}, *foreignTarget);
    ASSERT_TRUE(foreignFrame) << foreignFrame.error().message;

    auto target = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {16, 16}
    });
    ASSERT_TRUE(target) << target.error().message;
    const auto targetExtent = glm::uvec2 {16, 16};
    EXPECT_EQ(target->getExtent(), targetExtent);

    const auto unrenderedReadback = activeEngine().getOffscreenRenderer().readPixels(*target, {});
    ASSERT_FALSE(unrenderedReadback);
    EXPECT_EQ(unrenderedReadback.error().code, panda::ErrorCode::InvalidArgument);
    auto invalidView = panda::RenderView {};
    invalidView.camera.viewProjection = glm::mat4 {0.0F};
    const auto invalidFrame = activeEngine().getOffscreenRenderer().render(invalidView, *target);
    ASSERT_FALSE(invalidFrame);
    EXPECT_EQ(invalidFrame.error().code, panda::ErrorCode::InvalidArgument);

    const auto weak = std::weak_ptr {panda::detail::OffscreenTargetResource::getResource(*target)};
    auto copiedTarget = *target;
    auto assignedTarget = panda::OffscreenTarget {};
    assignedTarget = copiedTarget;
    const auto frame = activeEngine().getOffscreenRenderer().render(panda::RenderView {}, *target);
    ASSERT_TRUE(frame) << frame.error().message;
    const auto foreignCompletionReadback =
        activeEngine().getOffscreenRenderer().readPixels(*target, foreignFrame->completion);
    ASSERT_FALSE(foreignCompletionReadback);
    EXPECT_EQ(foreignCompletionReadback.error().code, panda::ErrorCode::InvalidArgument);
    target->reset();
    EXPECT_FALSE(weak.expired());
    copiedTarget.reset();
    assignedTarget.reset();
    EXPECT_FALSE(weak.expired());
    const auto waited = activeEngine().getSubmissionManager().wait(frame->completion, std::chrono::seconds {10});
    ASSERT_TRUE(waited) << waited.error().message;
    EXPECT_EQ(*waited, panda::WaitStatus::Completed);
    context.reset();
    EXPECT_TRUE(weak.expired());
}

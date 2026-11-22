#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/trigonometric.hpp>
#include <memory>
#include <optional>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/geometry/Sphere.hpp>
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

#include "EnvironmentLighting.hpp"
#include "LogCapture.hpp"
#include "resources/OffscreenTargetResource.hpp"
#include "resources/TextureResource.hpp"

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
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
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
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
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
    static constexpr auto composite = std::array {0.45F, 0.0F, 0.6F};
    return {toByte(encodeSrgb(toneMap(composite.at(0)))),
            toByte(encodeSrgb(toneMap(composite.at(1)))),
            toByte(encodeSrgb(toneMap(composite.at(2)))),
            255};
}

template <typename Value>
auto hasUnsupportedError(const std::expected<Value, panda::Error>& result) -> bool
{
    return !result && result.error().code == panda::ErrorCode::Unsupported;
}

auto pixelAt(const panda::ImageReadback& image, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4>
{
    const auto offset = ((static_cast<std::size_t>(y) * image.extent.x) + x) * 4U;
    return {image.rgba.at(offset), image.rgba.at(offset + 1), image.rgba.at(offset + 2), image.rgba.at(offset + 3)};
}

auto renderEveryFilteringPreset(const panda::Engine& engine,
                                const panda::RenderView& view,
                                const panda::OffscreenTarget& target) -> panda::Result<void>
{
    for (const auto preset : std::array {panda::TextureFilteringPreset::Nearest,
                                         panda::TextureFilteringPreset::Bilinear,
                                         panda::TextureFilteringPreset::Trilinear,
                                         panda::TextureFilteringPreset::Anisotropic2x,
                                         panda::TextureFilteringPreset::Anisotropic4x,
                                         panda::TextureFilteringPreset::Anisotropic8x,
                                         panda::TextureFilteringPreset::Anisotropic16x})
    {
        if (const auto selected = engine.setTextureFilteringPreset(preset); !selected)
        {
            return std::unexpected {selected.error()};
        }
        const auto frame = engine.getOffscreenRenderer().render(view, target);
        if (!frame)
        {
            return std::unexpected {frame.error()};
        }
        EXPECT_GE(frame->effectiveAnisotropy, 1.0F);
    }
    return {};
}

auto centerPixel(const panda::ImageReadback& image) -> std::array<std::uint8_t, 4>
{
    return pixelAt(image, image.extent.x / 2, image.extent.y / 2);
}

auto makeSkyboxCamera(glm::vec3 position, glm::vec3 target, float farPlane = 20.0F)
    -> panda::Result<panda::RenderCamera>
{
    return panda::createPerspectiveCamera({
        .position = position,
        .target = target,
        .up = {0.0F, 1.0F, 0.0F},
        .verticalFieldOfViewRadians = glm::radians(60.0F),
        .aspectRatio = 1.0F,
        .nearPlane = 0.1F,
        .farPlane = farPlane
    });
}

auto renderOffscreenPixels(const panda::OffscreenRenderer& renderer,
                           const panda::RenderView& view,
                           const panda::OffscreenTarget& target) -> panda::Result<panda::ImageReadback>
{
    const auto frame = renderer.render(view, target);
    if (!frame)
    {
        return std::unexpected {frame.error()};
    }
    return renderer.readPixels(target, frame->completion);
}

auto renderSpecularEnvironment(const panda::OffscreenRenderer& renderer,
                               const panda::OffscreenTarget& target,
                               const panda::RenderCamera& camera,
                               const panda::Texture& diffuse,
                               const panda::Texture& specular,
                               const panda::Mesh& sphere) -> panda::Result<panda::ImageReadback>
{
    auto draw = panda::MeshDraw {.mesh = sphere};
    draw.material.model = panda::MaterialModel::Lit;
    draw.material.renderFace = panda::RenderFace::Both;
    draw.material.metallic = 1.0F;
    draw.material.roughness = 0.65F;
    auto view = panda::RenderView {.camera = camera,
                                   .lights = {},
                                   .meshes = {std::move(draw)},
                                   .environment = {},
                                   .skybox = {}};
    view.environment = panda::EnvironmentLighting {.diffuseIrradiance = diffuse, .specularRadiance = specular};
    view.requestedSampleCount = panda::SampleCount::One;
    const auto frame = renderer.render(view, target);
    if (!frame)
    {
        return std::unexpected {frame.error()};
    }
    return renderer.readPixels(target, frame->completion);
}

auto adjacentFacePixelsDiffer(const panda::ImageReadback& first, const panda::ImageReadback& second) -> bool
{
    constexpr auto positions = std::array {
        glm::uvec2 {106U, 26U},
        glm::uvec2 {116U, 26U},
        glm::uvec2 {126U, 26U},
        glm::uvec2 {106U, 32U},
        glm::uvec2 {116U, 32U},
        glm::uvec2 {126U, 32U},
        glm::uvec2 {106U, 38U},
        glm::uvec2 {116U, 38U},
        glm::uvec2 {126U, 38U}
    };
    return std::ranges::any_of(positions, [&first, &second](const auto& position) {
        return pixelAt(first, position.x, position.y) != pixelAt(second, position.x, position.y);
    });
}

auto generatedEnvironmentSampleCountsDifferOnAdjacentFace(const panda::Engine& engine,
                                                          const panda::OffscreenRenderer& renderer,
                                                          const panda::OffscreenTarget& target,
                                                          const panda::RenderCamera& camera,
                                                          const panda::Texture& diffuse,
                                                          const panda::Mesh& sphere) -> panda::Result<bool>
{
    auto singleSampleSpecular = panda::test::createGeneratedSpecularEnvironmentFixture(engine, 1);
    if (!singleSampleSpecular)
    {
        return std::unexpected {singleSampleSpecular.error()};
    }
    auto filteredSpecular = panda::test::createGeneratedSpecularEnvironmentFixture(engine, 128);
    if (!filteredSpecular)
    {
        return std::unexpected {filteredSpecular.error()};
    }
    const auto singleSamplePixels =
        renderSpecularEnvironment(renderer, target, camera, diffuse, *singleSampleSpecular, sphere);
    if (!singleSamplePixels)
    {
        return std::unexpected {singleSamplePixels.error()};
    }
    const auto filteredPixels = renderSpecularEnvironment(renderer, target, camera, diffuse, *filteredSpecular, sphere);
    if (!filteredPixels)
    {
        return std::unexpected {filteredPixels.error()};
    }
    return adjacentFacePixelsDiffer(*singleSamplePixels, *filteredPixels);
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
    if (hasUnsupportedError(createdTarget))
    {
        GTEST_SKIP() << createdTarget.error().message;
    }
    ASSERT_TRUE(createdTarget) << createdTarget.error().message;
    auto target = std::move(*createdTarget);
    auto mesh = createBlendQuad(activeEngine());
    if (hasUnsupportedError(mesh))
    {
        GTEST_SKIP() << mesh.error().message;
    }
    ASSERT_TRUE(mesh) << mesh.error().message;
    auto firstView = makeBlendView(*mesh, true);
    ASSERT_TRUE(firstView) << firstView.error().message;
    const auto skybox = panda::test::createSkyboxFixture(activeEngine());
    if (hasUnsupportedError(skybox))
    {
        GTEST_SKIP() << skybox.error().message;
    }
    ASSERT_TRUE(skybox) << skybox.error().message;
    firstView->skybox = *skybox;
    const auto firstFrame = activeEngine().getOffscreenRenderer().render(*firstView, target);
    if (hasUnsupportedError(firstFrame))
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

    auto secondView = makeBlendView(*mesh, false);
    ASSERT_TRUE(secondView) << secondView.error().message;
    secondView->skybox = *skybox;
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
    generatedView->meshes.push_back(generatedView->meshes.front());
    const auto filteredPresets = renderEveryFilteringPreset(activeEngine(), *generatedView, *generatedTarget);
    ASSERT_TRUE(filteredPresets) << filteredPresets.error().message;
    ASSERT_TRUE(activeEngine().setTextureFilteringPreset(panda::TextureFilteringPreset::Anisotropic8x));
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

TEST_F(OffscreenRenderBackendSystem, AppliesDiffuseAndRoughnessFilteredEnvironmentLighting)
{
    constexpr auto extent = glm::uvec2 {192, 64};
    const auto target = activeEngine().getResourceManager().createOffscreenTarget({.extent = extent});
    if (!target && target.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << target.error().message;
    }
    ASSERT_TRUE(target) << target.error().message;
    auto environment = panda::test::createEnvironmentLightingFixture(activeEngine());
    if (!environment && environment.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << environment.error().message;
    }
    ASSERT_TRUE(environment) << environment.error().message;
    auto skybox = panda::test::createSkyboxFixture(activeEngine());
    if (!skybox && skybox.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << skybox.error().message;
    }
    ASSERT_TRUE(skybox) << skybox.error().message;
    const auto geometry = panda::createSphereGeometry({.longitudeSegments = 32, .latitudeRings = 16});
    ASSERT_TRUE(geometry) << geometry.error().message;
    const auto sphere = activeEngine().getResourceManager().createMeshFromGeometry(*geometry);
    ASSERT_TRUE(sphere) << sphere.error().message;
    auto upload = activeEngine().getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(upload) << upload.error().message;
    ASSERT_TRUE(upload->write(*sphere, *geometry));
    const auto uploaded = upload->submit();
    ASSERT_TRUE(uploaded) << uploaded.error().message;
    const auto uploadStatus = activeEngine().getSubmissionManager().wait(*uploaded, std::chrono::seconds {10});
    ASSERT_TRUE(uploadStatus) << uploadStatus.error().message;
    ASSERT_EQ(*uploadStatus, panda::WaitStatus::Completed);

    const auto camera = panda::createPerspectiveCamera({
        .position = {0.0F, 0.0F, 3.0F},
        .target = {},
        .up = {0.0F, 1.0F, 0.0F},
        .verticalFieldOfViewRadians = 0.7853982F,
        .aspectRatio = 3.0F,
        .nearPlane = 0.1F,
        .farPlane = 20.0F
    });
    ASSERT_TRUE(camera) << camera.error().message;
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
    view.environment = *environment;
    view.requestedSampleCount = panda::SampleCount::One;
    const auto& renderer = activeEngine().getOffscreenRenderer();
    const auto renderMaterial = [&](const panda::EnvironmentLighting& lighting,
                                    float metallic,
                                    float roughness) -> panda::Result<panda::ImageReadback> {
        view.environment = lighting;
        auto draw = panda::MeshDraw {.mesh = *sphere};
        draw.material.model = panda::MaterialModel::Lit;
        draw.material.renderFace = panda::RenderFace::Both;
        draw.material.metallic = metallic;
        draw.material.roughness = roughness;
        view.meshes = {std::move(draw)};
        const auto frame = renderer.render(view, *target);
        if (!frame)
        {
            return std::unexpected {frame.error()};
        }
        return renderer.readPixels(*target, frame->completion);
    };
    const auto smoothPixels = renderMaterial(*environment, 1.0F, 0.045F);
    ASSERT_TRUE(smoothPixels) << smoothPixels.error().message;
    const auto roughPixels = renderMaterial(*environment, 1.0F, 0.9F);
    ASSERT_TRUE(roughPixels) << roughPixels.error().message;
    const auto dielectricPixels = renderMaterial(*environment, 0.0F, 0.5F);
    ASSERT_TRUE(dielectricPixels) << dielectricPixels.error().message;
    const auto smoothMetal = centerPixel(*smoothPixels);
    const auto roughMetal = centerPixel(*roughPixels);
    const auto dielectric = centerPixel(*dielectricPixels);
    EXPECT_GT(smoothMetal.at(0), roughMetal.at(0) + 30U);
    const auto smoothShoulder = std::max(pixelAt(*smoothPixels, 91, 32).at(0), pixelAt(*smoothPixels, 101, 32).at(0));
    const auto roughShoulder = std::max(pixelAt(*roughPixels, 91, 32).at(0), pixelAt(*roughPixels, 101, 32).at(0));
    EXPECT_GT(static_cast<int>(smoothMetal.at(0)) - smoothShoulder,
              static_cast<int>(roughMetal.at(0)) - roughShoulder + 30);
    EXPECT_GT(dielectric.at(1), dielectric.at(0) + 40U);

    const auto sampleCountsDiffer = generatedEnvironmentSampleCountsDifferOnAdjacentFace(activeEngine(),
                                                                                         renderer,
                                                                                         *target,
                                                                                         *camera,
                                                                                         environment->diffuseIrradiance,
                                                                                         *sphere);
    ASSERT_TRUE(sampleCountsDiffer) << sampleCountsDiffer.error().message;
    EXPECT_TRUE(*sampleCountsDiffer);

    auto incomplete =
        panda::RenderView {.camera = *camera, .lights = {}, .meshes = view.meshes, .environment = {}, .skybox = {}};
    incomplete.environment = panda::EnvironmentLighting {.diffuseIrradiance = environment->diffuseIrradiance,
                                                         .specularRadiance = environment->diffuseIrradiance};
    const auto incompatible = renderer.render(incomplete, *target);
    ASSERT_FALSE(incompatible);
    EXPECT_EQ(incompatible.error().code, panda::ErrorCode::InvalidArgument);

    auto unuploaded = activeEngine().getResourceManager().createTexture({
        .extent = {8, 8},
        .format = panda::PixelFormat::Rgba16Float,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Supplied,
        .type = panda::TextureType::CubeMap
    });
    ASSERT_TRUE(unuploaded) << unuploaded.error().message;
    auto unuploadedSkyboxView =
        panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
    unuploadedSkyboxView.skybox = *unuploaded;
    const auto unuploadedSkyboxFrame = renderer.render(unuploadedSkyboxView, *target);
    ASSERT_FALSE(unuploadedSkyboxFrame);
    EXPECT_EQ(unuploadedSkyboxFrame.error().code, panda::ErrorCode::InvalidArgument);
    incomplete.environment->specularRadiance = *unuploaded;
    const auto notUploaded = renderer.render(incomplete, *target);
    ASSERT_FALSE(notUploaded);
    EXPECT_EQ(notUploaded.error().code, panda::ErrorCode::InvalidArgument);

    view.environment.reset();
    const auto ambientFrame = renderer.render(view, *target);
    ASSERT_TRUE(ambientFrame) << ambientFrame.error().message;
    const auto ambientPixels = renderer.readPixels(*target, ambientFrame->completion);
    ASSERT_TRUE(ambientPixels) << ambientPixels.error().message;
    EXPECT_GT(dielectric.at(1), centerPixel(*ambientPixels).at(1) + 40U);

    view.environment = *environment;
    view.skybox = *skybox;
    const auto combinedPixels = renderMaterial(*environment, 0.0F, 0.5F);
    ASSERT_TRUE(combinedPixels) << combinedPixels.error().message;
    EXPECT_EQ(centerPixel(*combinedPixels), dielectric);
    auto backgroundView =
        panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
    backgroundView.skybox = *skybox;
    const auto backgroundPixels = renderOffscreenPixels(renderer, backgroundView, *target);
    ASSERT_TRUE(backgroundPixels) << backgroundPixels.error().message;
    EXPECT_NE(centerPixel(*combinedPixels), centerPixel(*backgroundPixels));
    const auto weakSkybox = std::weak_ptr {panda::detail::TextureResource::getResource(*skybox)};
    const auto retainedFrame = renderer.render(view, *target);
    ASSERT_TRUE(retainedFrame) << retainedFrame.error().message;
    view.environment.reset();
    view.skybox.reset();
    environment->diffuseIrradiance.reset();
    environment->specularRadiance.reset();
    skybox->reset();
    EXPECT_FALSE(weakSkybox.expired());
    const auto retainedStatus =
        activeEngine().getSubmissionManager().wait(retainedFrame->completion, std::chrono::seconds {10});
    ASSERT_TRUE(retainedStatus) << retainedStatus.error().message;
    EXPECT_EQ(*retainedStatus, panda::WaitStatus::Completed);
}

TEST_F(OffscreenRenderBackendSystem, RendersSkyboxAndSolidBackgroundForEmptyViews)
{
    constexpr auto extent = glm::uvec2 {64, 64};
    const auto target = activeEngine().getResourceManager().createOffscreenTarget({.extent = extent});
    if (!target && target.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << target.error().message;
    }
    ASSERT_TRUE(target) << target.error().message;
    const auto skybox = panda::test::createSkyboxFixture(activeEngine());
    if (!skybox && skybox.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << skybox.error().message;
    }
    ASSERT_TRUE(skybox) << skybox.error().message;
    const auto camera = makeSkyboxCamera({0.0F, 0.0F, 3.0F}, {});
    ASSERT_TRUE(camera) << camera.error().message;
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
    view.skybox = *skybox;
    const auto& renderer = activeEngine().getOffscreenRenderer();
    const auto frame = renderer.render(view, *target);
    ASSERT_TRUE(frame) << frame.error().message;
    EXPECT_EQ(frame->effectiveSampleCount, panda::SampleCount::One);
    const auto pixels = renderer.readPixels(*target, frame->completion);
    ASSERT_TRUE(pixels) << pixels.error().message;
    const auto center = centerPixel(*pixels);
    const auto expectedChannel = toByte(encodeSrgb(toneMap(1.0F)));
    EXPECT_NEAR(center.at(0), expectedChannel, 3);
    EXPECT_LE(center.at(1), 3U);
    EXPECT_NEAR(center.at(2), expectedChannel, 3);
    EXPECT_EQ(center.at(3), 255U);
    const auto distantCamera = makeSkyboxCamera({0.0F, 0.0F, 3.0F}, {}, 10'000'000.0F);
    ASSERT_TRUE(distantCamera) << distantCamera.error().message;
    view.camera = *distantCamera;
    const auto distantPixels = renderOffscreenPixels(renderer, view, *target);
    ASSERT_TRUE(distantPixels) << distantPixels.error().message;
    EXPECT_EQ(centerPixel(*distantPixels), center);
    view.skybox.reset();
    const auto solidPixels = renderOffscreenPixels(renderer, view, *target);
    ASSERT_TRUE(solidPixels) << solidPixels.error().message;
    EXPECT_NE(centerPixel(*solidPixels), center);
}

TEST_F(OffscreenRenderBackendSystem, SkyboxUsesCameraRotationButIgnoresTranslation)
{
    constexpr auto extent = glm::uvec2 {64, 64};
    const auto target = activeEngine().getResourceManager().createOffscreenTarget({.extent = extent});
    if (!target && target.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << target.error().message;
    }
    ASSERT_TRUE(target) << target.error().message;
    const auto skybox = panda::test::createSkyboxFixture(activeEngine());
    ASSERT_TRUE(skybox) << skybox.error().message;
    const auto camera = makeSkyboxCamera({0.0F, 0.0F, 3.0F}, {});
    ASSERT_TRUE(camera) << camera.error().message;
    auto view = panda::RenderView {.camera = *camera, .lights = {}, .meshes = {}, .environment = {}, .skybox = {}};
    view.skybox = *skybox;
    const auto& renderer = activeEngine().getOffscreenRenderer();
    const auto original = renderOffscreenPixels(renderer, view, *target);
    ASSERT_TRUE(original) << original.error().message;
    const auto center = centerPixel(*original);
    const auto translatedCamera = makeSkyboxCamera({4.0F, 2.0F, 6.0F}, {4.0F, 2.0F, 5.0F});
    ASSERT_TRUE(translatedCamera) << translatedCamera.error().message;
    view.camera = *translatedCamera;
    const auto translatedPixels = renderOffscreenPixels(renderer, view, *target);
    ASSERT_TRUE(translatedPixels) << translatedPixels.error().message;
    EXPECT_EQ(centerPixel(*translatedPixels), center);

    const auto rotatedCamera = makeSkyboxCamera({0.0F, 0.0F, 3.0F}, {3.0F, 0.0F, 3.0F});
    ASSERT_TRUE(rotatedCamera) << rotatedCamera.error().message;
    view.camera = *rotatedCamera;
    const auto rotatedPixels = renderOffscreenPixels(renderer, view, *target);
    ASSERT_TRUE(rotatedPixels) << rotatedPixels.error().message;
    const auto rotatedCenter = centerPixel(*rotatedPixels);
    EXPECT_NE(rotatedCenter, center);
}

TEST_F(OffscreenRenderBackendSystem, RejectsIncompatibleSkyboxTexture)
{
    const auto target = activeEngine().getResourceManager().createOffscreenTarget({
        .extent = {16, 16}
    });
    if (!target && target.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << target.error().message;
    }
    ASSERT_TRUE(target) << target.error().message;
    const auto incompatible = activeEngine().getResourceManager().createTexture({
        .extent = {1, 1},
        .format = panda::PixelFormat::Rgba8Unorm,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled
    });
    if (!incompatible && incompatible.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << incompatible.error().message;
    }
    ASSERT_TRUE(incompatible) << incompatible.error().message;
    auto view = panda::RenderView {};
    view.skybox = *incompatible;
    const auto rejected = activeEngine().getOffscreenRenderer().render(view, *target);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, panda::ErrorCode::InvalidArgument);
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

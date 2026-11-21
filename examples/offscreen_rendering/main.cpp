#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <memory>
#include <panda/Engine.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/geometry/Cube.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <panda/tools/image/Writer.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
constexpr auto targetExtent = glm::uvec2 {640, 360};
constexpr auto frameCount = std::size_t {4};
constexpr auto filteringExtent = glm::uvec2 {960, 540};
constexpr auto filteringTextureExtent = glm::uvec2 {2048, 2048};

struct TextureFilteringScene
{
    panda::Mesh plane;
    panda::Texture stripes;
};

struct TextureFilteringChoice
{
    panda::TextureFilteringPreset preset;
    std::string_view name;
};

constexpr auto filteringChoices = std::array {
    TextureFilteringChoice {.preset = panda::TextureFilteringPreset::Nearest,       .name = "Nearest"       },
    TextureFilteringChoice {.preset = panda::TextureFilteringPreset::Bilinear,      .name = "Bilinear"      },
    TextureFilteringChoice {.preset = panda::TextureFilteringPreset::Trilinear,     .name = "Trilinear"     },
    TextureFilteringChoice {.preset = panda::TextureFilteringPreset::Anisotropic8x, .name = "Anisotropic 8x"}
};

auto stripeValue(std::uint32_t columnIndex) -> std::uint8_t
{
    const auto fineStripe = ((columnIndex / 4U) & 1U) == 0U;
    const auto wideStripe = ((columnIndex / 64U) & 1U) == 0U;
    if (fineStripe)
    {
        return wideStripe ? 240U : 160U;
    }
    return wideStripe ? 24U : 80U;
}

void writeStripeTexel(std::vector<std::byte>& pixels, std::uint32_t rowIndex, std::uint32_t columnIndex)
{
    const auto value = static_cast<std::byte>(stripeValue(columnIndex));
    const auto offset = ((static_cast<std::size_t>(rowIndex) * filteringTextureExtent.x) + columnIndex) * 4U;
    pixels.at(offset) = value;
    pixels.at(offset + 1U) = value;
    pixels.at(offset + 2U) = value;
    pixels.at(offset + 3U) = std::byte {0xff};
}

void writeFrame(std::size_t frameIndex, const panda::ImageReadback& pixels)
{
    const auto fileName = std::string {"offscreen_frame_"} + std::to_string(frameIndex);
    panda::expect(panda::tools::image::writePng(pixels, fileName + ".png"), "Write the offscreen PNG");
    panda::expect(panda::tools::image::writeRawRgba(pixels, fileName + ".rgba"), "Write the raw RGBA frame");
    panda::expect(panda::tools::image::writeBmp(pixels, fileName + ".bmp"), "Write the offscreen BMP");
    panda::expect(panda::tools::image::writeJpeg(pixels, fileName + ".jpg"), "Write the offscreen JPEG");
    panda::log::info("Wrote {}x{} image files for frame {}", pixels.extent.x, pixels.extent.y, frameIndex);
}

void renderSequence(const panda::Engine& context, const panda::Mesh& cube)
{
    const auto target = panda::expect(context.getResourceManager().createOffscreenTarget({.extent = targetExtent}),
                                      "Create the offscreen target");
    const auto camera =
        panda::expect(panda::createPerspectiveCamera({
                          .position = {2.5F, 1.8F, 2.5F},
                          .target = {},
                          .up = {0.0F, 1.0F, 0.0F},
                          .verticalFieldOfViewRadians = glm::radians(50.0F),
                          .aspectRatio = static_cast<float>(targetExtent.x) / static_cast<float>(targetExtent.y),
                          .nearPlane = 0.1F,
                          .farPlane = 20.0F
    }),
                      "Create the camera");
    auto view = panda::RenderView {.camera = camera,
                                   .lights = {panda::DirectionalLight {}},
                                   .meshes = {panda::MeshDraw {.mesh = cube}},
                                   .environment = {},
                                   .skybox = {}};
    const auto& renderer = context.getOffscreenRenderer();
    for (auto frameIndex = std::size_t {}; frameIndex < frameCount; ++frameIndex)
    {
        const auto angle = glm::radians(20.0F * static_cast<float>(frameIndex));
        view.meshes.front().modelMatrix = glm::rotate(glm::mat4 {1.0F}, angle, glm::vec3 {0.0F, 1.0F, 0.0F});
        const auto frame = panda::expect(renderer.render(view, target), "Render the offscreen frame");
        const auto pixels = panda::expect(renderer.readPixels(target, frame.completion), "Read the rendered pixels");
        writeFrame(frameIndex, pixels);
    }
}

auto makeStripeTexture() -> std::vector<std::byte>
{
    auto pixels =
        std::vector<std::byte>(static_cast<std::size_t>(filteringTextureExtent.x) * filteringTextureExtent.y * 4U);
    for (auto rowIndex = std::uint32_t {}; rowIndex < filteringTextureExtent.y; ++rowIndex)
    {
        for (auto columnIndex = std::uint32_t {}; columnIndex < filteringTextureExtent.x; ++columnIndex)
        {
            writeStripeTexel(pixels, rowIndex, columnIndex);
        }
    }
    return pixels;
}

auto createTextureFilteringScene(const panda::Engine& context) -> TextureFilteringScene
{
    auto geometry = panda::Geometry {
        .vertices = {{.position = {-30.0F, 0.0F, 8.0F}, .normal = {0.0F, 1.0F, 0.0F}, .uv = {0.0F, 0.0F}},
                     {.position = {30.0F, 0.0F, 8.0F}, .normal = {0.0F, 1.0F, 0.0F}, .uv = {16.0F, 0.0F}},
                     {.position = {30.0F, 0.0F, -150.0F}, .normal = {0.0F, 1.0F, 0.0F}, .uv = {16.0F, 64.0F}},
                     {.position = {-30.0F, 0.0F, -150.0F}, .normal = {0.0F, 1.0F, 0.0F}, .uv = {0.0F, 64.0F}}},
        .indices = std::vector<std::uint16_t> {0, 1, 2, 0, 2, 3}
    };
    const auto plane =
        panda::expect(context.getResourceManager().createMeshFromGeometry(geometry), "Create the receding plane mesh");
    const auto stripes =
        panda::expect(context.getResourceManager().createTexture(
                          {.extent = filteringTextureExtent,
                           .format = panda::PixelFormat::Rgba8Srgb,
                           .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
                           .mipPolicy = panda::TextureMipPolicy::Generate}),
                      "Create the procedural stripe texture");
    const auto pixels = makeStripeTexture();
    auto upload = panda::expect(context.getSubmissionManager().createUploadBatch(), "Create filtering demo uploads");
    panda::expect(upload.write(plane, geometry), "Upload the receding plane");
    panda::expect(upload.write(stripes, pixels), "Upload the procedural stripe texture");
    const auto completion = panda::expect(upload.submit(), "Submit filtering demo uploads");
    const auto status = panda::expect(context.getSubmissionManager().wait(completion, std::chrono::seconds {30}),
                                      "Wait for filtering demo uploads");
    panda::expect(status == panda::WaitStatus::Completed, "Filtering demo uploads did not finish in time");
    return {.plane = plane, .stripes = stripes};
}

auto composeFilteringComparison(const std::array<panda::ImageReadback, 4>& panels) -> panda::ImageReadback
{
    auto image = panda::ImageReadback {
        .extent = filteringExtent * 2U,
        .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(filteringExtent.x) * filteringExtent.y * 16U)};
    for (auto panel = std::size_t {}; panel < panels.size(); ++panel)
    {
        const auto panelX = static_cast<std::uint32_t>(panel % 2U) * filteringExtent.x;
        const auto panelY = static_cast<std::uint32_t>(panel / 2U) * filteringExtent.y;
        for (auto row = std::uint32_t {}; row < filteringExtent.y; ++row)
        {
            const auto sourceOffset = static_cast<std::size_t>(row) * filteringExtent.x * 4U;
            const auto destinationOffset = ((static_cast<std::size_t>(panelY + row) * image.extent.x) + panelX) * 4U;
            std::copy_n(panels.at(panel).rgba.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                        static_cast<std::size_t>(filteringExtent.x) * 4U,
                        image.rgba.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
        }
    }
    return image;
}

struct FilteringPanel
{
    panda::ImageReadback image;
    panda::SampleCount effectiveSampleCount {panda::SampleCount::One};
};

struct FilteringPanels
{
    std::array<panda::ImageReadback, filteringChoices.size()> images;
    panda::SampleCount effectiveSampleCount;
};

auto renderFilteringPanel(const panda::Engine& context,
                          const panda::RenderView& view,
                          const panda::OffscreenTarget& target,
                          const TextureFilteringChoice& choice) -> FilteringPanel
{
    panda::expect(context.setTextureFilteringPreset(choice.preset), "Set the comparison filtering preset");
    const auto frame =
        panda::expect(context.getOffscreenRenderer().render(view, target), "Render the texture filtering comparison");
    panda::log::info("{}: effective anisotropy cap {}x", choice.name, frame.effectiveAnisotropy);
    return {.image = panda::expect(context.getOffscreenRenderer().readPixels(target, frame.completion),
                                   "Read the texture filtering comparison panel"),
            .effectiveSampleCount = frame.effectiveSampleCount};
}

auto renderFilteringPanels(const panda::Engine& context,
                           const panda::RenderView& view,
                           const panda::OffscreenTarget& target) -> FilteringPanels
{
    auto result = FilteringPanels {.images = {}, .effectiveSampleCount = panda::SampleCount::One};
    for (auto index = std::size_t {}; index < filteringChoices.size(); ++index)
    {
        auto panel = renderFilteringPanel(context, view, target, filteringChoices.at(index));
        result.images.at(index) = std::move(panel.image);
        result.effectiveSampleCount = panel.effectiveSampleCount;
    }
    return result;
}

void writeFilteringComparison(const FilteringPanels& panels)
{
    const auto comparison = composeFilteringComparison(panels.images);
    panda::expect(panda::tools::image::writePng(comparison, "texture_filtering_comparison.png"),
                  "Write the texture filtering comparison PNG");
    std::cout << "Wrote texture_filtering_comparison.png (2x2 panels; MSAA "
              << static_cast<unsigned int>(panels.effectiveSampleCount) << "x in every panel)\n"
              << "Top left: Nearest, top right: Bilinear, bottom left: Trilinear, bottom right: Anisotropic 8x\n";
}

auto createFilteringView(const TextureFilteringScene& scene) -> panda::RenderView
{
    const auto camera =
        panda::expect(panda::createPerspectiveCamera({
                          .position = {0.0F, 4.0F, 8.0F  },
                          .target = {0.0F, 0.0F, -90.0F},
                          .up = {0.0F, 1.0F, 0.0F  },
                          .verticalFieldOfViewRadians = glm::radians(50.0F),
                          .aspectRatio = static_cast<float>(filteringExtent.x) / static_cast<float>(filteringExtent.y),
                          .nearPlane = 0.1F,
                          .farPlane = 250.0F
    }),
                      "Create the texture filtering comparison camera");
    auto draw = panda::MeshDraw {.mesh = scene.plane};
    draw.material.model = panda::MaterialModel::Unlit;
    draw.material.baseColorTexture = scene.stripes;
    draw.material.renderFace = panda::RenderFace::Both;
    auto view = panda::RenderView {.camera = camera, .lights = {}, .meshes = {draw}, .environment = {}, .skybox = {}};
    view.requestedSampleCount = panda::SampleCount::Four;
    return view;
}

void renderTextureFilteringComparison(const panda::Engine& context, const TextureFilteringScene& scene)
{
    const auto target = panda::expect(context.getResourceManager().createOffscreenTarget({.extent = filteringExtent}),
                                      "Create the texture filtering comparison target");
    const auto view = createFilteringView(scene);
    const auto panels = renderFilteringPanels(context, view, target);
    writeFilteringComparison(panels);
}
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto context = panda::expect(
            panda::Engine::create({.applicationName = "Panda offscreen rendering", .enableValidation = true}),
            "Create a headless Vulkan context");
        const auto geometry = panda::createCubeGeometry();
        const auto cube =
            panda::expect(context.getResourceManager().createMeshFromGeometry(geometry), "Create the cube mesh");
        auto batch = panda::expect(context.getSubmissionManager().createUploadBatch(), "Create the upload batch");
        panda::expect(batch.write(cube, geometry), "Stage the cube geometry");
        const auto completion = panda::expect(batch.submit(), "Submit the cube upload");
        const auto uploadStatus =
            panda::expect(context.getSubmissionManager().wait(completion, std::chrono::seconds {10}),
                          "Wait for the cube upload");
        panda::expect(uploadStatus == panda::WaitStatus::Completed, "Cube upload did not finish in time");
        renderSequence(context, cube);
        const auto filteringScene = createTextureFilteringScene(context);
        renderTextureFilteringComparison(context, filteringScene);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}

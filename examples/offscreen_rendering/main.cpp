#include <chrono>
#include <cstddef>
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
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <panda/tools/image/Writer.hpp>
#include <string>
#include <tuple>

namespace
{
constexpr auto targetExtent = glm::uvec2 {640, 360};
constexpr auto frameCount = std::size_t {4};

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
                                   .meshes = {panda::MeshDraw {.mesh = cube}}};
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
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}

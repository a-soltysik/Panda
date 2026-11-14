#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Camera.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <string>
#include <tuple>
#include <vulkan/vulkan.hpp>

namespace
{
constexpr auto targetExtent = vk::Extent2D {640, 360};
constexpr auto frameCount = std::size_t {4};

void writeFrame(std::size_t frameIndex, const panda::ImageReadback& pixels)
{
    const auto fileName = std::string {"offscreen_frame_"} + std::to_string(frameIndex) + ".rgba";
    auto file = std::ofstream {fileName, std::ios::binary};
    panda::expect(file.good(), "Open the raw RGBA output file");
    file.write(reinterpret_cast<const char*>(pixels.rgba.data()), static_cast<std::streamsize>(pixels.rgba.size()));
    panda::expect(file.good(), "Write the raw RGBA output file");
    panda::log::info("Wrote {}x{} RGBA pixels to '{}'", pixels.extent.width, pixels.extent.height, fileName);
}

void renderSequence(const panda::Context& context, const panda::Mesh& cube)
{
    const auto target = panda::expect(context.getResourceManager().createOffscreenTarget({.extent = targetExtent}),
                                      "Create the offscreen target");
    const auto camera = panda::expect(
        panda::createPerspectiveCamera({
            .position = {2.5F, 1.8F, 2.5F},
            .target = {},
            .up = {0.0F, 1.0F, 0.0F},
            .verticalFieldOfViewRadians = glm::radians(50.0F),
            .aspectRatio = static_cast<float>(targetExtent.width) / static_cast<float>(targetExtent.height),
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
            panda::Context::create({.applicationName = "Panda offscreen rendering", .enableValidation = true}),
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

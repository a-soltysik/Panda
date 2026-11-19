#include "VulkanSmokeRunBackend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numbers>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <panda/geometry/Cube.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/geometry/Plane.hpp>
#include <panda/geometry/Sphere.hpp>
#include <panda/rendering/Camera.hpp>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/WindowRenderer.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "SmokeRunBackend.hpp"
#include "panda_bench/Arguments.hpp"
#include "panda_bench/Report.hpp"

namespace panda_bench
{
namespace
{
constexpr auto pointShadowResolution = std::uint32_t {256};

struct SceneObject
{
    bool sphere;
    std::array<float, 3> position;
    std::array<float, 3> scale;
    std::array<float, 4> baseColor;
};

struct SceneMeshes
{
    panda::Mesh plane;
    panda::Mesh cube;
    panda::Mesh sphere;
};

constexpr auto sceneObjects = std::array {
    SceneObject {.sphere = false,
                 .position = {-2.2F, 0.6F, -1.8F},
                 .scale = {1.2F, 1.2F, 1.2F},
                 .baseColor = {0.82F, 0.16F, 0.12F, 1.0F}},
    SceneObject {.sphere = true,
                 .position = {0.0F, 0.7F, -1.8F},
                 .scale = {1.4F, 1.4F, 1.4F},
                 .baseColor = {0.12F, 0.46F, 0.78F, 1.0F}},
    SceneObject {.sphere = false,
                 .position = {2.2F, 0.5F, -1.8F},
                 .scale = {1.0F, 1.0F, 1.0F},
                 .baseColor = {0.9F, 0.47F, 0.08F, 1.0F} },
    SceneObject {.sphere = true,
                 .position = {-2.2F, 0.6F, 0.0F},
                 .scale = {1.2F, 1.2F, 1.2F},
                 .baseColor = {0.18F, 0.68F, 0.34F, 1.0F}},
    SceneObject {.sphere = false,
                 .position = {0.0F, 1.0F, 0.0F},
                 .scale = {1.1F, 2.0F, 1.1F},
                 .baseColor = {0.72F, 0.2F, 0.58F, 1.0F} },
    SceneObject {.sphere = true,
                 .position = {2.2F, 0.7F, 0.0F},
                 .scale = {1.4F, 1.4F, 1.4F},
                 .baseColor = {0.12F, 0.72F, 0.76F, 1.0F}},
    SceneObject {.sphere = false,
                 .position = {-2.2F, 0.5F, 1.8F},
                 .scale = {1.0F, 1.0F, 1.0F},
                 .baseColor = {0.86F, 0.72F, 0.12F, 1.0F}},
    SceneObject {.sphere = true,
                 .position = {0.0F, 0.55F, 1.8F},
                 .scale = {1.1F, 1.1F, 1.1F},
                 .baseColor = {0.42F, 0.24F, 0.82F, 1.0F}},
    SceneObject {.sphere = false,
                 .position = {2.2F, 0.6F, 1.8F},
                 .scale = {1.2F, 1.2F, 1.2F},
                 .baseColor = {0.85F, 0.28F, 0.2F, 1.0F} }
};

auto backendFailure(std::string message) -> Failure
{
    return {.message = std::move(message)};
}

auto unsupported(std::string message) -> Failure
{
    return {.outcome = "unsupported", .message = std::move(message)};
}

auto failFromPanda(const panda::Error& error) -> Failure
{
    if (error.code == panda::ErrorCode::Unsupported)
    {
        return unsupported(error.message);
    }
    return backendFailure(error.message);
}

auto statusFromPanda(panda::FrameResult::Status status) -> FrameStatus
{
    switch (status)
    {
    case panda::FrameResult::Status::Rendered:
        return FrameStatus::Presented;
    case panda::FrameResult::Status::Skipped:
        return FrameStatus::Skipped;
    case panda::FrameResult::Status::Suspended:
        return FrameStatus::Suspended;
    case panda::FrameResult::Status::RecreatePending:
        return FrameStatus::RecreatePending;
    }
    return FrameStatus::Skipped;
}

auto configureSceneCamera(panda::RenderView& view, glm::uvec2 extent) -> std::expected<void, Failure>
{
    const auto cameraPosition = glm::vec3 {4.5F, 4.0F, 8.0F};
    const auto camera = panda::createPerspectiveCamera({
        .position = cameraPosition,
        .target = {0.0F, 0.7F, 0.0F},
        .up = {0,    1,    0   },
        .verticalFieldOfViewRadians = glm::radians(50.0F),
        .aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y),
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    });
    if (!camera)
    {
        return std::unexpected {failFromPanda(camera.error())};
    }
    view.camera = *camera;
    return {};
}

auto createPointLight(std::uint32_t index, const Options& options) -> panda::PointLight
{
    const auto pointLightCount = options.lightCount - 1U;
    const auto angle =
        std::numbers::pi_v<float> * 2.0F * static_cast<float>(index) / static_cast<float>(pointLightCount);
    auto light = panda::PointLight {
        .position = {7.0F * std::cos(angle), 3.0F, 7.0F * std::sin(angle)},
        .color = glm::vec3 {1.0F},
        .intensity = 2.0F,
        .range = 8.0F,
        .shadow = {}
    };
    if (index + 1U < options.shadowedLightCount)
    {
        light.shadow = panda::ShadowSettings {.resolution = pointShadowResolution};
    }
    return light;
}

void configureLights(panda::RenderView& view, const Options& options)
{
    if (options.lightCount == 0)
    {
        return;
    }
    view.lights.reserve(options.lightCount);
    auto directionalLight = panda::DirectionalLight {.direction = glm::normalize(glm::vec3 {-0.4F, -1.0F, -0.3F}),
                                                     .color = glm::vec3 {1.0F},
                                                     .intensity = 3.0F,
                                                     .shadow = {}};
    if (options.shadowedLightCount > 0)
    {
        directionalLight.shadow = panda::ShadowSettings {};
    }
    view.lights.emplace_back(directionalLight);
    const auto pointLightCount = options.lightCount - 1U;
    for (auto index = std::uint32_t {}; index < pointLightCount; ++index)
    {
        view.lights.emplace_back(createPointLight(index, options));
    }
}

auto waitForGeometryUpload(const panda::Engine& context, const panda::CompletionPoint& completion)
    -> std::expected<void, Failure>
{
    const auto waited = context.getSubmissionManager().wait(completion, std::chrono::seconds {30});
    if (!waited)
    {
        return std::unexpected {failFromPanda(waited.error())};
    }
    if (*waited == panda::WaitStatus::Timeout)
    {
        return std::unexpected {backendFailure("A benchmark mesh upload did not complete within 30 seconds")};
    }
    return {};
}

auto uploadGeometry(const panda::Engine& context, panda::Mesh& mesh, const panda::Geometry& geometry)
    -> std::expected<void, Failure>
{
    auto uploadBatch = context.getSubmissionManager().createUploadBatch();
    if (!uploadBatch)
    {
        return std::unexpected {failFromPanda(uploadBatch.error())};
    }
    if (const auto written = uploadBatch->write(mesh, geometry); !written)
    {
        return std::unexpected {failFromPanda(written.error())};
    }
    const auto completion = uploadBatch->submit();
    if (!completion)
    {
        return std::unexpected {failFromPanda(completion.error())};
    }
    return waitForGeometryUpload(context, *completion);
}

auto createUploadedMesh(const panda::Engine& context, const panda::Geometry& geometry)
    -> std::expected<panda::Mesh, Failure>
{
    auto mesh = context.getResourceManager().createMeshFromGeometry(geometry);
    if (!mesh)
    {
        return std::unexpected {failFromPanda(mesh.error())};
    }
    if (const auto uploaded = uploadGeometry(context, *mesh, geometry); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return std::move(*mesh);
}

auto modelMatrix(glm::vec3 position, glm::vec3 scale) -> glm::mat4
{
    return glm::translate(glm::mat4 {1.0F}, position) * glm::scale(glm::mat4 {1.0F}, scale);
}

void appendDraw(panda::RenderView& view, const panda::Mesh& mesh, glm::mat4 transform, glm::vec4 baseColor)
{
    auto material = panda::Material {};
    material.baseColor = baseColor;
    material.roughness = 0.72F;
    view.meshes.push_back({.mesh = mesh, .modelMatrix = transform, .material = std::move(material)});
}

auto createSceneMeshes(const panda::Engine& context) -> std::expected<SceneMeshes, Failure>
{
    auto plane = createUploadedMesh(context, panda::createPlaneGeometry());
    if (!plane)
    {
        return std::unexpected {plane.error()};
    }
    auto cube = createUploadedMesh(context, panda::createCubeGeometry());
    if (!cube)
    {
        return std::unexpected {cube.error()};
    }
    auto sphereGeometry = panda::createSphereGeometry();
    if (!sphereGeometry)
    {
        return std::unexpected {failFromPanda(sphereGeometry.error())};
    }
    auto sphere = createUploadedMesh(context, *sphereGeometry);
    if (!sphere)
    {
        return std::unexpected {sphere.error()};
    }
    return SceneMeshes {.plane = std::move(*plane), .cube = std::move(*cube), .sphere = std::move(*sphere)};
}

void appendSceneDraws(panda::RenderView& view, const SceneMeshes& meshes)
{
    appendDraw(view, meshes.plane, modelMatrix({0.0F, -0.02F, 0.0F}, {12.0F, 1.0F, 12.0F}), {0.3F, 0.32F, 0.35F, 1.0F});
    for (const auto& object : sceneObjects)
    {
        const auto& mesh = object.sphere ? meshes.sphere : meshes.cube;
        appendDraw(
            view,
            mesh,
            modelMatrix(
                glm::vec3 {std::get<0>(object.position), std::get<1>(object.position), std::get<2>(object.position)},
                glm::vec3 {std::get<0>(object.scale), std::get<1>(object.scale), std::get<2>(object.scale)}),
            glm::vec4 {std::get<0>(object.baseColor),
                       std::get<1>(object.baseColor),
                       std::get<2>(object.baseColor),
                       std::get<3>(object.baseColor)});
    }
}

auto addUploadedScene(const panda::Engine& context, panda::RenderView& view) -> std::expected<void, Failure>
{
    auto meshes = createSceneMeshes(context);
    if (!meshes)
    {
        return std::unexpected {meshes.error()};
    }
    appendSceneDraws(view, *meshes);
    return {};
}
}

auto VulkanSmokeRunBackend::initialize(const Options& options) -> std::expected<SmokeRunEnvironment, Failure>
{
    if (const auto window = createWindow(options); !window)
    {
        return std::unexpected {window.error()};
    }
    if (const auto context = createEngine(options); !context)
    {
        return std::unexpected {context.error()};
    }
    const auto environment = captureEnvironment();
    if (!environment)
    {
        return std::unexpected {environment.error()};
    }
    if (const auto scene = createStaticScene(environment->framebufferExtent, options); !scene)
    {
        return std::unexpected {scene.error()};
    }
    return environment;
}

auto VulkanSmokeRunBackend::shouldClose() const -> bool
{
    return checkedWindow().shouldClose();
}

auto VulkanSmokeRunBackend::getFramebufferExtent() const -> std::expected<glm::uvec2, Failure>
{
    const auto extent = checkedWindow().getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {failFromPanda(extent.error())};
    }
    return *extent;
}

auto VulkanSmokeRunBackend::effectiveSampleCount() const noexcept -> std::uint32_t
{
    return _effectiveSampleCount;
}

auto VulkanSmokeRunBackend::submitFrame() -> std::expected<FrameStatus, Failure>
{
    const auto& renderer = checkedEngine().getWindowRenderer();
    const auto frame = renderer.render(_view);
    if (!frame)
    {
        return std::unexpected {failFromPanda(frame.error())};
    }
    if (frame->status == panda::FrameResult::Status::Rendered && !frame->completion.isValid())
    {
        return std::unexpected {backendFailure("A presented frame had no completion token")};
    }
    if (frame->completion.isValid())
    {
        _lastCompletion = frame->completion;
    }
    _effectiveSampleCount = static_cast<std::uint32_t>(frame->effectiveSampleCount);
    return statusFromPanda(frame->status);
}

auto VulkanSmokeRunBackend::takeAvailableSamples() -> std::expected<std::vector<panda::FramePerformanceSample>, Failure>
{
    const auto samples = checkedEngine().getPerformanceManager().takeAvailableSamples();
    if (!samples)
    {
        return std::unexpected {failFromPanda(samples.error())};
    }
    return *samples;
}

auto VulkanSmokeRunBackend::processInput() -> std::expected<void, Failure>
{
    const auto input = checkedWindow().processInput();
    if (!input)
    {
        return std::unexpected {failFromPanda(input.error())};
    }
    return {};
}

auto VulkanSmokeRunBackend::waitForLastFrame(std::chrono::nanoseconds timeout) -> std::expected<void, Failure>
{
    const auto waited = checkedEngine().getSubmissionManager().wait(_lastCompletion, timeout);
    if (!waited)
    {
        return std::unexpected {failFromPanda(waited.error())};
    }
    if (*waited == panda::WaitStatus::Timeout)
    {
        return std::unexpected {backendFailure("The last submitted frame did not finish within 30 seconds")};
    }
    return {};
}

void VulkanSmokeRunBackend::teardown()
{
    _view.meshes.clear();
    _engine.reset();
    _window.reset();
    _lastCompletion = {};
    _effectiveSampleCount = 1;
}

auto VulkanSmokeRunBackend::checkedWindow() const -> const panda::tools::Window&
{
    if (!_window.has_value())
    {
        throw std::logic_error {"The benchmark window is not initialized"};
    }
    return *_window;
}

auto VulkanSmokeRunBackend::checkedEngine() -> panda::Engine&
{
    if (!_engine.has_value())
    {
        throw std::logic_error {"The benchmark context is not initialized"};
    }
    return *_engine;
}

auto VulkanSmokeRunBackend::createWindow(const Options& options) -> std::expected<void, Failure>
{
    auto createdWindow =
        panda::tools::Window::create(glm::uvec2 {options.windowWidth, options.windowHeight}, "Panda benchmark smoke");
    if (!createdWindow)
    {
        return std::unexpected {failFromPanda(createdWindow.error())};
    }
    _window.emplace(std::move(*createdWindow));
    return {};
}

auto VulkanSmokeRunBackend::createEngine(const Options& options) -> std::expected<void, Failure>
{
    auto createdEngine = panda::Engine::createWithSurface(
        checkedWindow(),
        {.applicationName = "Panda benchmark",
         .enableValidation = false,
         .enableFramePerformanceSampling = true,
         .maxLights = std::max(panda::EngineOptions::defaultMaxLights, options.lightCount),
         .presentationMode = options.presentationMode});
    if (!createdEngine)
    {
        return std::unexpected {failFromPanda(createdEngine.error())};
    }
    _engine.emplace(std::move(*createdEngine));
    return checkPerformanceCapabilities();
}

auto VulkanSmokeRunBackend::checkPerformanceCapabilities() -> std::expected<void, Failure>
{
    const auto capabilities = checkedEngine().getFeatures().performance;
    if (!capabilities.hasGpuTimestampQueries || !capabilities.isGpuTimestampSamplingEnabled)
    {
        return std::unexpected {unsupported("The selected graphics queue does not support GPU timestamp sampling")};
    }
    return {};
}

auto VulkanSmokeRunBackend::captureEnvironment() -> std::expected<SmokeRunEnvironment, Failure>
{
    const auto extent = getFramebufferExtent();
    if (!extent)
    {
        return std::unexpected {extent.error()};
    }
    if (extent->x == 0 || extent->y == 0)
    {
        return std::unexpected {
            Failure {.outcome = "invalid", .message = "The benchmark window is minimized"}
        };
    }
    const auto presentationMode = checkedEngine().getWindowRenderer().getEffectivePresentationMode();
    if (!presentationMode)
    {
        return std::unexpected {backendFailure("Benchmark window has no active swapchain")};
    }
    const auto deviceInfo = checkedEngine().getDeviceInfo();
    auto environment = SmokeRunEnvironment {.framebufferExtent = *extent,
                                            .gpuModel = deviceInfo.name,
                                            .driver = {},
                                            .vulkanApiVersion = deviceInfo.apiVersion,
                                            .presentationMode = *presentationMode};
    if (!deviceInfo.driverName.empty())
    {
        environment.driver = DriverReport {.name = deviceInfo.driverName,
                                           .info = deviceInfo.driverInfo,
                                           .version = deviceInfo.driverVersion};
    }
    return environment;
}

auto VulkanSmokeRunBackend::createStaticScene(glm::uvec2 extent, const Options& options) -> std::expected<void, Failure>
{
    auto view = panda::RenderView {};
    view.requestedSampleCount = options.sampleCount;
    if (const auto camera = configureSceneCamera(view, extent); !camera)
    {
        return std::unexpected {camera.error()};
    }
    configureLights(view, options);

    if (const auto scene = addUploadedScene(checkedEngine(), view); !scene)
    {
        return scene;
    }

    _view = std::move(view);
    return {};
}
}

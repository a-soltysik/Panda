#include "VulkanSmokeRunBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numbers>
#include <panda/Camera.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Cube.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
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
    case panda::FrameResult::Status::Presented:
        return FrameStatus::Presented;
    case panda::FrameResult::Status::Rendered:
    case panda::FrameResult::Status::Skipped:
        return FrameStatus::Skipped;
    case panda::FrameResult::Status::Suspended:
        return FrameStatus::Suspended;
    case panda::FrameResult::Status::RecreatePending:
        return FrameStatus::RecreatePending;
    }
    return FrameStatus::Skipped;
}

auto configureCubeCamera(panda::RenderView& view, panda::FramebufferExtent extent) -> std::expected<void, Failure>
{
    const auto cameraPosition = glm::vec3 {3.0F, 2.5F, 4.0F};
    const auto camera = panda::createPerspectiveCamera({
        .position = cameraPosition,
        .target = {},
        .up = {0, 1, 0},
        .verticalFieldOfViewRadians = glm::radians(50.0F),
        .aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height),
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

void configureLights(panda::RenderView& view, std::uint32_t lightCount)
{
    if (lightCount == 0)
    {
        return;
    }
    view.lights.reserve(lightCount);
    view.lights.emplace_back(panda::DirectionalLight {.direction = glm::normalize(glm::vec3 {-0.4F, -1.0F, -0.3F}),
                                                      .color = glm::vec3 {1.0F},
                                                      .intensity = 3.0F,
                                                      .shadow = panda::ShadowSettings {}});
    const auto pointLightCount = lightCount - 1U;
    for (auto index = std::uint32_t {}; index < pointLightCount; ++index)
    {
        const auto angle =
            std::numbers::pi_v<float> * 2.0F * static_cast<float>(index) / static_cast<float>(pointLightCount);
        view.lights.emplace_back(panda::PointLight {
            .position = {7.0F * std::cos(angle), 3.0F, 7.0F * std::sin(angle)},
            .color = glm::vec3 {1.0F},
            .intensity = 2.0F,
            .range = 8.0F,
            .shadow = {}
        });
    }
}

auto waitForCubeUpload(const panda::Engine& context, const panda::CompletionPoint& completion)
    -> std::expected<void, Failure>
{
    const auto waited = context.getSubmissionManager().wait(completion, std::chrono::seconds {30});
    if (!waited)
    {
        return std::unexpected {failFromPanda(waited.error())};
    }
    if (*waited == panda::WaitStatus::Timeout)
    {
        return std::unexpected {backendFailure("The cube mesh upload did not complete within 30 seconds")};
    }
    return {};
}

auto uploadCube(const panda::Engine& context, panda::Mesh& mesh, const panda::Geometry& geometry)
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
    return waitForCubeUpload(context, *completion);
}

auto addUploadedCube(const panda::Engine& context, panda::RenderView& view) -> std::expected<void, Failure>
{
    const auto geometry = panda::createCubeGeometry();
    auto mesh = context.getResourceManager().createMeshFromGeometry(geometry);
    if (!mesh)
    {
        return std::unexpected {failFromPanda(mesh.error())};
    }
    view.meshes.push_back({.mesh = std::move(*mesh)});
    return uploadCube(context, view.meshes.back().mesh, geometry);
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
    if (const auto cube = createStaticCube(environment->framebufferExtent, options.lightCount); !cube)
    {
        return std::unexpected {cube.error()};
    }
    return environment;
}

auto VulkanSmokeRunBackend::shouldClose() const -> bool
{
    return checkedWindow().shouldClose();
}

auto VulkanSmokeRunBackend::getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, Failure>
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
    const auto renderer = checkedEngine().getWindowRenderer();
    if (!renderer)
    {
        return std::unexpected {backendFailure("The benchmark context has no window renderer")};
    }
    const auto frame = renderer->get().render(_view);
    if (!frame)
    {
        return std::unexpected {failFromPanda(frame.error())};
    }
    if (frame->status == panda::FrameResult::Status::Presented && !frame->completion.isValid())
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
         .maxLights = std::max(panda::EngineOptions::defaultMaxLights, options.lightCount)});
    if (!createdEngine)
    {
        return std::unexpected {failFromPanda(createdEngine.error())};
    }
    _engine.emplace(std::move(*createdEngine));
    return checkPerformanceCapabilities();
}

auto VulkanSmokeRunBackend::checkPerformanceCapabilities() -> std::expected<void, Failure>
{
    const auto capabilities = checkedEngine().getPerformanceManager().getCapabilities();
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
    const auto deviceInfo = checkedEngine().getDeviceInfo();
    auto environment = SmokeRunEnvironment {.framebufferExtent = *extent,
                                            .gpuModel = deviceInfo.name,
                                            .driver = {},
                                            .vulkanApiVersion = deviceInfo.apiVersion};
    if (!deviceInfo.driverName.empty())
    {
        environment.driver = DriverReport {.name = deviceInfo.driverName,
                                           .info = deviceInfo.driverInfo,
                                           .version = deviceInfo.driverVersion};
    }
    return environment;
}

auto VulkanSmokeRunBackend::createStaticCube(panda::FramebufferExtent extent, std::uint32_t lightCount)
    -> std::expected<void, Failure>
{
    if (extent.width == 0 || extent.height == 0)
    {
        return {};
    }
    auto view = panda::RenderView {};
    if (const auto camera = configureCubeCamera(view, extent); !camera)
    {
        return std::unexpected {camera.error()};
    }
    configureLights(view, lightCount);

    if (const auto cube = addUploadedCube(checkedEngine(), view); !cube)
    {
        return cube;
    }

    _view = std::move(view);
    return {};
}
}

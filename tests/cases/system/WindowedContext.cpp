#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <optional>
#include <panda/Camera.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/Plane.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <panda/tools/gui/Window.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "GlfwTestSupport.hpp"
#include "LogCapture.hpp"

namespace
{

class SuspensibleSurface final : public panda::WindowSurface
{
public:
    explicit SuspensibleSurface(panda::tools::Window& window)
        : _window {window}
    {
    }

    void setSuspended(bool suspended) noexcept
    {
        _suspended = suspended;
    }

    [[nodiscard]] auto getRequiredInstanceExtensions() const
        -> std::expected<std::vector<std::string>, panda::Error> override
    {
        return _window.getRequiredInstanceExtensions();
    }

    [[nodiscard]] auto createSurface(vk::Instance instance) const
        -> std::expected<vk::SurfaceKHR, panda::Error> override
    {
        return _window.createSurface(instance);
    }

    [[nodiscard]] auto getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, panda::Error> override
    {
        if (_suspended)
        {
            return panda::FramebufferExtent {};
        }
        return _window.getFramebufferExtent();
    }

private:
    panda::tools::Window& _window;
    bool _suspended {false};
};

class FailingSurface final : public panda::WindowSurface
{
public:
    explicit FailingSurface(panda::tools::Window& window, bool returnNull)
        : _window {window},
          _returnNull {returnNull}
    {
    }

    [[nodiscard]] auto getRequiredInstanceExtensions() const
        -> std::expected<std::vector<std::string>, panda::Error> override
    {
        return _window.getRequiredInstanceExtensions();
    }

    [[nodiscard]] auto createSurface([[maybe_unused]] vk::Instance instance) const
        -> std::expected<vk::SurfaceKHR, panda::Error> override
    {
        if (_returnNull)
        {
            return vk::SurfaceKHR {};
        }
        return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure,
                                                 "native surface unavailable",
                                                 panda::NativeError {.api = "test surface", .code = 17})};
    }

    [[nodiscard]] auto getFramebufferExtent() const -> std::expected<panda::FramebufferExtent, panda::Error> override
    {
        return _window.getFramebufferExtent();
    }

private:
    panda::tools::Window& _window;
    bool _returnNull;
};

void verifyCompletion(const panda::Context& context, panda::CompletionPoint point)
{
    EXPECT_TRUE(point.isValid());
    const auto completed = context.wait(point, std::chrono::seconds {10});
    ASSERT_TRUE(completed);
    EXPECT_EQ(*completed, panda::WaitStatus::Completed);
    const auto polled = context.poll(point);
    ASSERT_TRUE(polled);
    EXPECT_TRUE(*polled);
}

struct SceneMeshes
{
    panda::Mesh cube;
    panda::Mesh ground;
    panda::Mesh triangle;
};

auto createCompactTriangle(const panda::Context& context) -> panda::Result<panda::Mesh>
{
    return context.createMesh({
        .vertexCount = 3,
        .vertexStride = 28,
        .indices = std::nullopt,
        .attributes = {{.semantic = panda::VertexAttributeSemantic::Position,
                        .format = panda::VertexAttributeFormat::Float3,
                        .offset = 0},
                       {.semantic = panda::VertexAttributeSemantic::Color,
                        .format = panda::VertexAttributeFormat::Float4,
                        .offset = 12}}
    });
}

auto finishSceneMeshUpload(const panda::Context& context, panda::UploadBatch& batch) -> panda::Result<void>
{
    const auto completion = batch.submit();
    if (!completion)
    {
        return std::unexpected {completion.error()};
    }
    const auto waited = context.wait(*completion, std::chrono::seconds {10});
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (*waited != panda::WaitStatus::Completed)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "Scene mesh upload did not complete")};
    }
    return {};
}

auto uploadSceneMeshes(const panda::Context& context,
                       const SceneMeshes& meshes,
                       const panda::Geometry& cubeGeometry,
                       const panda::Geometry& groundGeometry) -> panda::Result<void>
{
    static constexpr auto triangleVertices =
        std::array {-0.5F, -0.5F, 0.0F, 1.0F, 0.2F, 0.2F, 1.0F, 0.5F, -0.5F, 0.0F, 0.2F,
                    1.0F,  0.2F,  1.0F, 0.0F, 0.5F, 0.0F, 0.2F, 0.2F, 1.0F,  1.0F};
    auto batch = context.createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = batch->write(meshes.cube, cubeGeometry); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto written = batch->write(meshes.ground, groundGeometry); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto written = batch->write(meshes.triangle, std::as_bytes(std::span {triangleVertices})); !written)
    {
        return std::unexpected {written.error()};
    }
    return finishSceneMeshUpload(context, *batch);
}

auto createUploadedCubeGroundAndCompactTriangle(const panda::Context& context) -> panda::Result<SceneMeshes>
{
    const auto cubeGeometry = panda::createCubeGeometry();
    const auto groundGeometry = panda::createPlaneGeometry();
    auto cube = context.createMeshFromGeometry(cubeGeometry);
    if (!cube)
    {
        return std::unexpected {cube.error()};
    }
    auto ground = context.createMeshFromGeometry(groundGeometry);
    if (!ground)
    {
        return std::unexpected {ground.error()};
    }
    auto triangle = createCompactTriangle(context);
    if (!triangle)
    {
        return std::unexpected {triangle.error()};
    }
    auto meshes =
        SceneMeshes {.cube = std::move(*cube), .ground = std::move(*ground), .triangle = std::move(*triangle)};
    if (const auto uploaded = uploadSceneMeshes(context, meshes, cubeGeometry, groundGeometry); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return meshes;
}

auto makeCubeAndGroundView(panda::FramebufferExtent extent, SceneMeshes meshes) -> panda::Result<panda::RenderView>
{
    const auto aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const auto camera = panda::createPerspectiveCamera({
        .position = {3.0F, 2.5F, 4.0F},
        .target = {},
        .up = {0, 1, 0},
        .verticalFieldOfViewRadians = glm::radians(50.0F),
        .aspectRatio = aspectRatio,
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    });
    if (!camera)
    {
        return std::unexpected {camera.error()};
    }
    auto view = panda::RenderView {};
    view.camera = *camera;
    const auto groundTransform =
        glm::scale(glm::translate(glm::mat4 {1.0F}, glm::vec3 {0.0F, -0.5F, 0.0F}), glm::vec3 {10.0F, 1.0F, 10.0F});
    view.meshes = {
        panda::MeshDraw {.mesh = std::move(meshes.ground),
                         .modelMatrix = groundTransform,
                         .tint = {0.65F, 0.67F, 0.7F, 1.0F}},
        panda::MeshDraw {.mesh = meshes.cube},
        panda::MeshDraw {.mesh = meshes.cube,
                         .modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {-1.5F, 0.0F, 0.0F}),
                         .material = {.renderFace = panda::RenderFace::Back}},
        panda::MeshDraw {.mesh = std::move(meshes.cube),
                         .modelMatrix = glm::scale(glm::translate(glm::mat4 {1.0F}, glm::vec3 {1.5F, 0.0F, 0.0F}),
                         glm::vec3 {-1.0F, 1.0F, 1.0F}),
                         .material = {.renderFace = panda::RenderFace::Both}},
        panda::MeshDraw {.mesh = std::move(meshes.triangle),
                         .modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {-1.5F, -0.25F, 0.0F}),
                         .material = {.model = panda::MaterialModel::Unlit}}
    };
    return view;
}

}

TEST(WindowedContextSystem, CreatesValidatedSurfaceAndDevice)
{
    auto window = panda::tools::Window::create({160, 120}, "Panda Vulkan window smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    auto surface = SuspensibleSurface {*window};
    auto context =
        panda::Context::createWithSurface(surface, {.applicationName = "Panda window smoke", .enableValidation = true});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context.has_value()) << context.error().message;
    const auto beforeMove = context->render(panda::RenderView {});
    ASSERT_TRUE(beforeMove);
    auto movedContext = std::move(*context);
    verifyCompletion(movedContext, beforeMove->completion);
    EXPECT_FALSE(context->isValid());
    EXPECT_NE(movedContext.getDeviceInfo().swapchainMaintenance, panda::ContextDeviceInfo::SwapchainMaintenance::None);
    for (auto index = 0; index < 3; ++index)
    {
        ASSERT_TRUE(window->processInput().has_value());
        const auto frame = movedContext.render(panda::RenderView {});
        ASSERT_TRUE(frame.has_value()) << frame.error().message;
        EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
        verifyCompletion(movedContext, frame->completion);
    }
    ASSERT_TRUE(window->setSize({240, 180}).has_value());
    ASSERT_TRUE(window->processInput().has_value());
    const auto resized = movedContext.render(panda::RenderView {});
    ASSERT_TRUE(resized.has_value()) << resized.error().message;
    EXPECT_EQ(resized->status, panda::FrameResult::Status::Presented);
    EXPECT_TRUE(resized->completion.isValid());

    // Native iconification is asynchronous and may be ignored by a window manager.
    // The surface reports the same zero framebuffer state deterministically.
    surface.setSuspended(true);
    const auto suspended = movedContext.render(panda::RenderView {});
    ASSERT_TRUE(suspended.has_value());
    EXPECT_EQ(suspended->status, panda::FrameResult::Status::Suspended);
    EXPECT_FALSE(suspended->completion.isValid());

    surface.setSuspended(false);
    const auto restored = movedContext.render(panda::RenderView {});
    ASSERT_TRUE(restored.has_value()) << restored.error().message;
    EXPECT_EQ(restored->status, panda::FrameResult::Status::Presented);
    EXPECT_TRUE(restored->completion.isValid());
}

TEST(WindowedContextSystem, ReportsSurfaceCreationFailures)
{
    auto window = panda::tools::Window::create({160, 120}, "Panda surface error smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    auto failing = FailingSurface {*window, false};
    const auto failed = panda::Context::createWithSurface(failing);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(failed.error().native.has_value());
    const auto native = failed.error().native.value_or(panda::NativeError {});
    EXPECT_EQ(native.api, "test surface");
    EXPECT_EQ(native.code, 17);

    auto nullSurface = FailingSurface {*window, true};
    const auto empty = panda::Context::createWithSurface(nullSurface);
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, panda::ErrorCode::BackendFailure);
    EXPECT_NE(empty.error().message.find("null Vulkan surface"), std::string::npos);
}

TEST(WindowedContextSystem, RendersFaceModesAndReflectedMeshesWithValidation)
{
    auto records = panda::test::LogRecords {};
    auto capture = panda::test::ProcessSinkRegistration {records};
    auto window = panda::tools::Window::create({160, 120}, "Panda mesh render smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    auto surface = SuspensibleSurface {*window};
    {
        auto created =
            panda::Context::createWithSurface(surface,
                                              {.applicationName = "Panda mesh render smoke", .enableValidation = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created.has_value()) << created.error().message;
        auto context = std::move(*created);
        auto meshes = createUploadedCubeGroundAndCompactTriangle(context);
        ASSERT_TRUE(meshes.has_value()) << meshes.error().message;
        const auto extent = window->getFramebufferExtent();
        ASSERT_TRUE(extent.has_value()) << extent.error().message;
        auto view = makeCubeAndGroundView(*extent, std::move(*meshes));
        ASSERT_TRUE(view.has_value()) << view.error().message;
        const auto frame = context.render(*view);
        ASSERT_TRUE(frame.has_value()) << frame.error().message;
        EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
        EXPECT_TRUE(frame->effectiveSampleCount == vk::SampleCountFlagBits::e1 ||
                    frame->effectiveSampleCount == vk::SampleCountFlagBits::e2 ||
                    frame->effectiveSampleCount == vk::SampleCountFlagBits::e4);
        ASSERT_TRUE(window->setSize({240, 180}));
        ASSERT_TRUE(window->processInput());
        const auto resizedExtent = window->getFramebufferExtent();
        ASSERT_TRUE(resizedExtent);
        EXPECT_NE(resizedExtent->width, extent->width);
        EXPECT_NE(resizedExtent->height, extent->height);
        const auto resizedFrame = context.render(*view);
        ASSERT_TRUE(resizedFrame.has_value()) << resizedFrame.error().message;
        EXPECT_EQ(resizedFrame->status, panda::FrameResult::Status::Presented);
        view->meshes.clear();
        verifyCompletion(context, frame->completion);
        verifyCompletion(context, resizedFrame->completion);
    }
    const auto validationError = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(validationError, records.entries.end())
        << (validationError == records.entries.end() ? "" : validationError->message);
    const auto unusedVertexAttributeWarning = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Warning &&
               entry.message.find("Vertex attribute at location") != std::string::npos &&
               entry.message.find("not consumed by vertex shader") != std::string::npos;
    });
    EXPECT_EQ(unusedVertexAttributeWarning, records.entries.end())
        << (unusedVertexAttributeWarning == records.entries.end() ? "" : unusedVertexAttributeWarning->message);
}

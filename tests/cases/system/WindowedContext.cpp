#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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
#include <panda/Texture.hpp>
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
    panda::Texture baseColorMap;
    panda::Texture metallicRoughnessMap;
    panda::Texture normalMap;
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

auto createSceneTexture(const panda::Context& context, vk::Format format) -> panda::Result<panda::Texture>
{
    return context.createTexture({
        .extent = vk::Extent2D {2, 2},
        .format = format,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    });
}

void expectInvalidEmissiveMap(const panda::Context& context,
                              const panda::RenderCamera& camera,
                              const panda::Mesh& mesh,
                              const panda::Texture& texture)
{
    auto view = panda::RenderView {.camera = camera, .directionalLight = {}, .meshes = {}};
    auto draw = panda::MeshDraw {.mesh = mesh};
    draw.material.emissiveTexture = texture;
    draw.material.emissive = {1.0F, 1.0F, 1.0F};
    view.meshes.push_back(draw);
    const auto result = context.render(view);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

void expectInvalidNormalMap(const panda::Context& context,
                            const panda::RenderCamera& camera,
                            const panda::Mesh& mesh,
                            const panda::Texture& texture,
                            panda::MaterialModel model = panda::MaterialModel::Lit)
{
    auto view = panda::RenderView {.camera = camera, .directionalLight = {}, .meshes = {}};
    auto draw = panda::MeshDraw {.mesh = mesh};
    draw.material.model = model;
    draw.material.normalTexture = texture;
    view.meshes.push_back(draw);
    const auto result = context.render(view);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

void expectInvalidOcclusionMap(const panda::Context& context,
                               const panda::RenderCamera& camera,
                               const panda::Mesh& mesh,
                               const panda::Texture& texture,
                               panda::MaterialModel model = panda::MaterialModel::Lit)
{
    auto view = panda::RenderView {.camera = camera, .directionalLight = {}, .meshes = {}};
    auto draw = panda::MeshDraw {.mesh = mesh};
    draw.material.model = model;
    draw.material.occlusionTexture = texture;
    view.meshes.push_back(draw);
    const auto result = context.render(view);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
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

auto writeSceneNormalMap(panda::UploadBatch& batch, const panda::Texture& normalMap) -> panda::Result<void>
{
    static constexpr auto pixels = std::array {std::byte {0x80},
                                               std::byte {0x80},
                                               std::byte {0xff},
                                               std::byte {0xff},
                                               std::byte {0x80},
                                               std::byte {0x80},
                                               std::byte {0xff},
                                               std::byte {0xff},
                                               std::byte {0x80},
                                               std::byte {0x80},
                                               std::byte {0xff},
                                               std::byte {0xff},
                                               std::byte {0x80},
                                               std::byte {0x80},
                                               std::byte {0xff},
                                               std::byte {0xff}};
    return batch.write(normalMap, pixels);
}

auto writeSceneTextureMaps(panda::UploadBatch& batch, const SceneMeshes& meshes) -> panda::Result<void>
{
    static constexpr auto baseColorPixels = std::array {std::byte {0xff},
                                                        std::byte {0x20},
                                                        std::byte {0x20},
                                                        std::byte {0xff},
                                                        std::byte {0x20},
                                                        std::byte {0xff},
                                                        std::byte {0x20},
                                                        std::byte {0x00},
                                                        std::byte {0x20},
                                                        std::byte {0x20},
                                                        std::byte {0xff},
                                                        std::byte {0xff},
                                                        std::byte {0xff},
                                                        std::byte {0xff},
                                                        std::byte {0xff},
                                                        std::byte {0x00}};
    if (const auto written = batch.write(meshes.baseColorMap, baseColorPixels); !written)
    {
        return std::unexpected {written.error()};
    }
    static constexpr auto metallicRoughnessPixels = std::array {std::byte {0xff},
                                                                std::byte {0x80},
                                                                std::byte {0x40},
                                                                std::byte {0xff},
                                                                std::byte {0xff},
                                                                std::byte {0x40},
                                                                std::byte {0xc0},
                                                                std::byte {0xff},
                                                                std::byte {0xff},
                                                                std::byte {0x80},
                                                                std::byte {0x80},
                                                                std::byte {0xff},
                                                                std::byte {0xff},
                                                                std::byte {0xff},
                                                                std::byte {0xff},
                                                                std::byte {0xff}};
    if (const auto written = batch.write(meshes.metallicRoughnessMap, metallicRoughnessPixels); !written)
    {
        return std::unexpected {written.error()};
    }
    return writeSceneNormalMap(batch, meshes.normalMap);
}

auto uploadSceneMeshes(const panda::Context& context,
                       const SceneMeshes& meshes,
                       const panda::Geometry& cubeGeometry,
                       const panda::Geometry& groundGeometry) -> panda::Result<void>
{
    static constexpr auto triangleVertices =
        std::array {-0.5F, -0.5F, 0.0F, 1.0F, 0.2F, 0.2F, 1.0F, 0.5F, -0.5F, 0.0F, 0.2F,
                    1.0F,  0.2F,  1.0F, 0.0F, 0.5F, 0.0F, 0.2F, 0.2F, 1.0F,  0.0F};
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
    if (const auto written = writeSceneTextureMaps(*batch, meshes); !written)
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
    auto baseColorMap = createSceneTexture(context, vk::Format::eR8G8B8A8Srgb);
    if (!baseColorMap)
    {
        return std::unexpected {baseColorMap.error()};
    }
    auto metallicRoughnessMap = createSceneTexture(context, vk::Format::eR8G8B8A8Unorm);
    if (!metallicRoughnessMap)
    {
        return std::unexpected {metallicRoughnessMap.error()};
    }
    auto normalMap = createSceneTexture(context, vk::Format::eR8G8B8A8Unorm);
    if (!normalMap)
    {
        return std::unexpected {normalMap.error()};
    }
    auto meshes = SceneMeshes {.cube = std::move(*cube),
                               .ground = std::move(*ground),
                               .triangle = std::move(*triangle),
                               .baseColorMap = std::move(*baseColorMap),
                               .metallicRoughnessMap = std::move(*metallicRoughnessMap),
                               .normalMap = std::move(*normalMap)};
    if (const auto uploaded = uploadSceneMeshes(context, meshes, cubeGeometry, groundGeometry); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return meshes;
}

auto normalMapFallbackVertices(bool hasUv) -> std::span<const float>
{
    static constexpr auto withUv =
        std::array {-0.5F, -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.5F, -0.5F, 0.0F, 0.0F,
                    0.0F,  1.0F,  1.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.0F, 1.0F,  0.5F, 1.0F};
    static constexpr auto withoutUv = std::array {-0.5F,
                                                  -0.5F,
                                                  0.0F,
                                                  0.0F,
                                                  0.0F,
                                                  1.0F,
                                                  0.5F,
                                                  -0.5F,
                                                  0.0F,
                                                  0.0F,
                                                  0.0F,
                                                  1.0F,
                                                  0.0F,
                                                  0.5F,
                                                  0.0F,
                                                  0.0F,
                                                  0.0F,
                                                  1.0F};
    return hasUv ? std::span<const float> {withUv} : std::span<const float> {withoutUv};
}

auto createNormalMapFallbackMesh(const panda::Context& context, bool hasUv) -> panda::Result<panda::Mesh>
{
    auto attributes = std::vector<panda::VertexAttribute> {
        {.semantic = panda::VertexAttributeSemantic::Position, .format = panda::VertexAttributeFormat::Float3},
        {.semantic = panda::VertexAttributeSemantic::Normal,
         .format = panda::VertexAttributeFormat::Float3,
         .offset = 12}
    };
    if (hasUv)
    {
        attributes.push_back({.semantic = panda::VertexAttributeSemantic::TexCoord0,
                              .format = panda::VertexAttributeFormat::Float2,
                              .offset = 24});
    }
    auto mesh = context.createMesh(
        {.vertexCount = 3, .vertexStride = hasUv ? 32U : 24U, .indices = std::nullopt, .attributes = attributes});
    if (!mesh)
    {
        return std::unexpected {mesh.error()};
    }
    auto batch = context.createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = batch->write(*mesh, std::as_bytes(normalMapFallbackVertices(hasUv))); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto uploaded = finishSceneMeshUpload(context, *batch); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return std::move(*mesh);
}

auto makeTestCamera(panda::FramebufferExtent extent) -> panda::Result<panda::RenderCamera>
{
    const auto aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return panda::createPerspectiveCamera({
        .position = {3.0F, 2.5F, 4.0F},
        .target = {},
        .up = {0, 1, 0},
        .verticalFieldOfViewRadians = glm::radians(50.0F),
        .aspectRatio = aspectRatio,
        .nearPlane = 0.1F,
        .farPlane = 100.0F
    });
}

auto makeBlendDraws(const SceneMeshes& meshes) -> std::vector<panda::MeshDraw>
{
    auto first = panda::MeshDraw {};
    first.mesh = meshes.cube;
    first.modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {0.0F, 0.0F, 0.5F});
    first.material.baseColor = {1.0F, 1.0F, 1.0F, 0.5F};
    first.material.alphaMode = panda::AlphaMode::Blend;
    auto second = panda::MeshDraw {};
    second.mesh = meshes.cube;
    second.modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {0.0F, 0.0F, 0.2F});
    second.material.baseColor = {0.8F, 0.5F, 1.0F, 0.5F};
    second.material.alphaMode = panda::AlphaMode::Blend;
    return {first, second};
}

auto makeFaceDraws(const SceneMeshes& meshes) -> std::vector<panda::MeshDraw>
{
    const auto reflectedModel = glm::scale(glm::mat4 {1.0F}, glm::vec3 {-1.0F, 1.0F, 1.0F});
    auto frontFaceOnly = panda::MeshDraw {};
    frontFaceOnly.mesh = meshes.cube;
    frontFaceOnly.modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {-1.5F, 0.0F, 0.0F});
    frontFaceOnly.material.renderFace = panda::RenderFace::Back;
    auto bothFaces = panda::MeshDraw {};
    bothFaces.mesh = meshes.cube;
    bothFaces.modelMatrix =
        glm::scale(glm::translate(glm::mat4 {1.0F}, glm::vec3 {1.5F, 0.0F, 0.0F}), glm::vec3 {-1.0F, 1.0F, 1.0F});
    bothFaces.material.renderFace = panda::RenderFace::Both;
    auto reflectedBackFaceOnly = panda::MeshDraw {};
    reflectedBackFaceOnly.mesh = meshes.cube;
    reflectedBackFaceOnly.modelMatrix = glm::translate(reflectedModel, glm::vec3 {0.0F, 0.0F, -1.5F});
    reflectedBackFaceOnly.material.renderFace = panda::RenderFace::Back;
    return {
        frontFaceOnly,
        bothFaces,
        panda::MeshDraw {.mesh = meshes.cube, .modelMatrix = reflectedModel},
        reflectedBackFaceOnly
    };
}

auto makeTexturedMaskDraw(const SceneMeshes& meshes) -> panda::MeshDraw
{
    auto draw = panda::MeshDraw {};
    draw.mesh = meshes.cube;
    draw.modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {1.5F, 0.0F, -1.5F});
    draw.material.baseColorTexture = meshes.baseColorMap;
    draw.material.metallicRoughnessTexture = meshes.metallicRoughnessMap;
    draw.material.metallic = 0.75F;
    draw.material.roughness = 0.8F;
    draw.material.renderFace = panda::RenderFace::Both;
    draw.material.alphaMode = panda::AlphaMode::Mask;
    return draw;
}

auto makeUnlitMaskDraw(const SceneMeshes& meshes) -> panda::MeshDraw
{
    auto draw = panda::MeshDraw {};
    draw.mesh = meshes.triangle;
    draw.modelMatrix = glm::translate(glm::mat4 {1.0F}, glm::vec3 {-1.5F, -0.25F, 0.0F});
    draw.material.model = panda::MaterialModel::Unlit;
    draw.material.alphaMode = panda::AlphaMode::Mask;
    return draw;
}

auto makeCubeAndGroundView(panda::FramebufferExtent extent, const SceneMeshes& meshes)
    -> panda::Result<panda::RenderView>
{
    const auto camera = makeTestCamera(extent);
    if (!camera)
    {
        return std::unexpected {camera.error()};
    }
    const auto groundTransform =
        glm::scale(glm::translate(glm::mat4 {1.0F}, glm::vec3 {0.0F, -0.5F, 0.0F}), glm::vec3 {10.0F, 1.0F, 10.0F});
    auto view = panda::RenderView {};
    view.camera = *camera;
    view.meshes = {
        panda::MeshDraw {.mesh = meshes.ground, .modelMatrix = groundTransform, .tint = {0.65F, 0.67F, 0.7F, 1.0F}},
        panda::MeshDraw {.mesh = meshes.cube}
    };
    auto blendDraws = makeBlendDraws(meshes);
    view.meshes.insert(view.meshes.end(), blendDraws.begin(), blendDraws.end());
    auto faceDraws = makeFaceDraws(meshes);
    view.meshes.insert(view.meshes.end(), faceDraws.begin(), faceDraws.end());
    view.meshes.push_back(makeTexturedMaskDraw(meshes));
    view.meshes.push_back(makeUnlitMaskDraw(meshes));
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

TEST(WindowedContextSystem, RetainsTextureAfterApplicationReleasesItUntilFrameCompletion)
{
    auto records = panda::test::LogRecords {};
    auto capture = panda::test::ProcessSinkRegistration {records};
    auto window = panda::tools::Window::create({160, 120}, "Panda texture retention smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    auto surface = SuspensibleSurface {*window};
    {
        auto created = panda::Context::createWithSurface(
            surface,
            {.applicationName = "Panda texture retention smoke", .enableValidation = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created.has_value()) << created.error().message;
        auto context = std::move(*created);
        auto meshes = createUploadedCubeGroundAndCompactTriangle(context);
        if (!meshes && meshes.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << meshes.error().message;
        }
        ASSERT_TRUE(meshes.has_value()) << meshes.error().message;
        auto texture = createSceneTexture(context, vk::Format::eR8G8B8A8Unorm);
        ASSERT_TRUE(texture.has_value()) << texture.error().message;
        auto batch = context.createUploadBatch();
        ASSERT_TRUE(batch.has_value()) << batch.error().message;
        static constexpr auto pixels = std::array {std::byte {0xff},
                                                   std::byte {0x80},
                                                   std::byte {0x40},
                                                   std::byte {0xff},
                                                   std::byte {0xff},
                                                   std::byte {0x80},
                                                   std::byte {0x40},
                                                   std::byte {0xff},
                                                   std::byte {0xff},
                                                   std::byte {0x80},
                                                   std::byte {0x40},
                                                   std::byte {0xff},
                                                   std::byte {0xff},
                                                   std::byte {0x80},
                                                   std::byte {0x40},
                                                   std::byte {0xff}};
        ASSERT_TRUE(batch->write(*texture, pixels));
        ASSERT_TRUE(finishSceneMeshUpload(context, *batch));

        const auto extent = window->getFramebufferExtent();
        ASSERT_TRUE(extent.has_value()) << extent.error().message;
        const auto camera = makeTestCamera(*extent);
        ASSERT_TRUE(camera.has_value()) << camera.error().message;
        auto view = panda::RenderView {.camera = *camera, .directionalLight = {}, .meshes = {}};
        auto draw = panda::MeshDraw {.mesh = meshes->cube};
        draw.material.normalTexture = *texture;
        view.meshes.push_back(draw);

        const auto frame = context.render(view);
        ASSERT_TRUE(frame.has_value()) << frame.error().message;
        texture->reset();
        view.meshes.clear();
        verifyCompletion(context, frame->completion);
    }
    const auto validationError = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(validationError, records.entries.end())
        << (validationError == records.entries.end() ? "" : validationError->message);
}

TEST(WindowedContextSystem, RendersNormalMapsAndFallsBackWithoutUvOrTangents)
{
    auto records = panda::test::LogRecords {};
    auto capture = panda::test::ProcessSinkRegistration {records};
    auto window = panda::tools::Window::create({160, 120}, "Panda normal map smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    auto surface = SuspensibleSurface {*window};
    auto created =
        panda::Context::createWithSurface(surface,
                                          {.applicationName = "Panda normal map smoke", .enableValidation = true});
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
    auto view = makeCubeAndGroundView(*extent, *meshes);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    expectInvalidNormalMap(context, view->camera, meshes->cube, meshes->normalMap, panda::MaterialModel::Unlit);
    auto srgbNormalMap = createSceneTexture(context, vk::Format::eR8G8B8A8Srgb);
    ASSERT_TRUE(srgbNormalMap.has_value()) << srgbNormalMap.error().message;
    expectInvalidNormalMap(context, view->camera, meshes->cube, *srgbNormalMap);
    auto unuploadedNormalMap = createSceneTexture(context, vk::Format::eR8G8B8A8Unorm);
    ASSERT_TRUE(unuploadedNormalMap.has_value()) << unuploadedNormalMap.error().message;
    expectInvalidNormalMap(context, view->camera, meshes->cube, *unuploadedNormalMap);
    auto noUvMesh = createNormalMapFallbackMesh(context, false);
    ASSERT_TRUE(noUvMesh.has_value()) << noUvMesh.error().message;
    auto noTangentMesh = createNormalMapFallbackMesh(context, true);
    ASSERT_TRUE(noTangentMesh.has_value()) << noTangentMesh.error().message;
    auto normalMappedDraw = panda::MeshDraw {};
    normalMappedDraw.mesh = meshes->cube;
    normalMappedDraw.material.normalTexture = meshes->normalMap;
    view->meshes.push_back(normalMappedDraw);
    auto noUvDraw = panda::MeshDraw {};
    noUvDraw.mesh = *noUvMesh;
    noUvDraw.material.normalTexture = meshes->normalMap;
    view->meshes.push_back(noUvDraw);
    auto noTangentDraw = panda::MeshDraw {};
    noTangentDraw.mesh = *noTangentMesh;
    noTangentDraw.material.normalTexture = meshes->normalMap;
    view->meshes.push_back(noTangentDraw);

    const auto frame = context.render(*view);

    ASSERT_TRUE(frame.has_value()) << frame.error().message;
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
    verifyCompletion(context, frame->completion);
    const auto missingUvWarning = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Warning && entry.message.find("no TexCoord0") != std::string::npos;
    });
    EXPECT_NE(missingUvWarning, records.entries.end());
    const auto missingTangentWarning = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Warning && entry.message.find("no Tangent") != std::string::npos;
    });
    EXPECT_NE(missingTangentWarning, records.entries.end());
    const auto validationError = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(validationError, records.entries.end())
        << (validationError == records.entries.end() ? "" : validationError->message);
}

TEST(WindowedContextSystem, PresentsBlendedAndMaskedAlphaFaceModesAndReflectedMeshesWithoutValidationErrors)
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
        if (!meshes && meshes.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << meshes.error().message;
        }
        ASSERT_TRUE(meshes.has_value()) << meshes.error().message;
        const auto extent = window->getFramebufferExtent();
        ASSERT_TRUE(extent.has_value()) << extent.error().message;
        auto view = makeCubeAndGroundView(*extent, *meshes);
        ASSERT_TRUE(view.has_value()) << view.error().message;
        auto baseColorDraw = panda::MeshDraw {};
        baseColorDraw.mesh = meshes->cube;
        baseColorDraw.material.baseColorTexture = meshes->baseColorMap;
        view->meshes.push_back(baseColorDraw);
        auto metallicRoughnessDraw = panda::MeshDraw {};
        metallicRoughnessDraw.mesh = meshes->cube;
        metallicRoughnessDraw.material.metallicRoughnessTexture = meshes->metallicRoughnessMap;
        metallicRoughnessDraw.material.metallic = 0.75F;
        metallicRoughnessDraw.material.roughness = 0.8F;
        view->meshes.push_back(metallicRoughnessDraw);
        auto occlusionDraw = panda::MeshDraw {.mesh = meshes->cube};
        occlusionDraw.material.occlusionTexture = meshes->metallicRoughnessMap;
        occlusionDraw.material.occlusionStrength = 0.75F;
        view->meshes.push_back(occlusionDraw);
        auto litEmissiveDraw = panda::MeshDraw {.mesh = meshes->cube};
        litEmissiveDraw.material.emissiveTexture = meshes->baseColorMap;
        litEmissiveDraw.material.emissive = {1.0F, 0.8F, 0.3F};
        view->meshes.push_back(litEmissiveDraw);
        auto unlitEmissiveDraw = panda::MeshDraw {.mesh = meshes->cube};
        unlitEmissiveDraw.material.model = panda::MaterialModel::Unlit;
        unlitEmissiveDraw.material.emissiveTexture = meshes->baseColorMap;
        unlitEmissiveDraw.material.emissive = {1.0F, 0.8F, 0.3F};
        view->meshes.push_back(unlitEmissiveDraw);
        auto missingUvView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto missingUvDraw = panda::MeshDraw {};
        missingUvDraw.mesh = meshes->triangle;
        missingUvDraw.material.model = panda::MaterialModel::Unlit;
        missingUvDraw.material.baseColorTexture = meshes->baseColorMap;
        missingUvView.meshes.push_back(missingUvDraw);
        const auto missingUvResult = context.render(missingUvView);
        ASSERT_FALSE(missingUvResult.has_value());
        EXPECT_EQ(missingUvResult.error().code, panda::ErrorCode::InvalidArgument);

        auto uvlessLitMesh = context.createMesh({
            .vertexCount = 3,
            .vertexStride = 24,
            .indices = std::nullopt,
            .attributes = {{.semantic = panda::VertexAttributeSemantic::Position,
                            .format = panda::VertexAttributeFormat::Float3,
                            .offset = 0},
                           {.semantic = panda::VertexAttributeSemantic::Normal,
                            .format = panda::VertexAttributeFormat::Float3,
                            .offset = 12}}
        });
        ASSERT_TRUE(uvlessLitMesh.has_value()) << uvlessLitMesh.error().message;
        expectInvalidEmissiveMap(context, view->camera, *uvlessLitMesh, meshes->baseColorMap);
        expectInvalidOcclusionMap(context, view->camera, *uvlessLitMesh, meshes->metallicRoughnessMap);
        auto missingUvMrView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto missingUvMrDraw = panda::MeshDraw {};
        missingUvMrDraw.mesh = *uvlessLitMesh;
        missingUvMrDraw.material.metallicRoughnessTexture = meshes->metallicRoughnessMap;
        missingUvMrView.meshes.push_back(missingUvMrDraw);
        const auto missingUvMrResult = context.render(missingUvMrView);
        ASSERT_FALSE(missingUvMrResult.has_value());
        EXPECT_EQ(missingUvMrResult.error().code, panda::ErrorCode::InvalidArgument);

        auto unlitMrView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto unlitMrDraw = panda::MeshDraw {};
        unlitMrDraw.mesh = meshes->cube;
        unlitMrDraw.material.model = panda::MaterialModel::Unlit;
        unlitMrDraw.material.metallicRoughnessTexture = meshes->metallicRoughnessMap;
        unlitMrView.meshes.push_back(unlitMrDraw);
        const auto unlitMrResult = context.render(unlitMrView);
        ASSERT_FALSE(unlitMrResult.has_value());
        EXPECT_EQ(unlitMrResult.error().code, panda::ErrorCode::InvalidArgument);
        expectInvalidOcclusionMap(context,
                                  view->camera,
                                  meshes->cube,
                                  meshes->metallicRoughnessMap,
                                  panda::MaterialModel::Unlit);
        expectInvalidOcclusionMap(context, view->camera, meshes->cube, meshes->baseColorMap);

        auto linearMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
        });
        ASSERT_TRUE(linearMap.has_value()) << linearMap.error().message;
        expectInvalidEmissiveMap(context, view->camera, meshes->cube, *linearMap);
        auto linearMapView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto linearBaseColorDraw = panda::MeshDraw {};
        linearBaseColorDraw.mesh = meshes->cube;
        linearBaseColorDraw.material.baseColorTexture = *linearMap;
        linearMapView.meshes.push_back(linearBaseColorDraw);
        const auto linearMapResult = context.render(linearMapView);
        ASSERT_FALSE(linearMapResult.has_value());
        EXPECT_EQ(linearMapResult.error().code, panda::ErrorCode::InvalidArgument);

        auto srgbMetallicRoughnessMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Srgb,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
        });
        ASSERT_TRUE(srgbMetallicRoughnessMap.has_value()) << srgbMetallicRoughnessMap.error().message;
        auto srgbMrView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto srgbMrDraw = panda::MeshDraw {};
        srgbMrDraw.mesh = meshes->cube;
        srgbMrDraw.material.metallicRoughnessTexture = *srgbMetallicRoughnessMap;
        srgbMrView.meshes.push_back(srgbMrDraw);
        const auto srgbMrResult = context.render(srgbMrView);
        ASSERT_FALSE(srgbMrResult.has_value());
        EXPECT_EQ(srgbMrResult.error().code, panda::ErrorCode::InvalidArgument);

        auto unsampledMetallicRoughnessMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment
        });
        ASSERT_TRUE(unsampledMetallicRoughnessMap.has_value()) << unsampledMetallicRoughnessMap.error().message;
        expectInvalidOcclusionMap(context, view->camera, meshes->cube, *unsampledMetallicRoughnessMap);
        auto unsampledEmissiveMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Srgb,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment
        });
        ASSERT_TRUE(unsampledEmissiveMap.has_value()) << unsampledEmissiveMap.error().message;
        expectInvalidEmissiveMap(context, view->camera, meshes->cube, *unsampledEmissiveMap);
        auto unsampledMrView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto unsampledMrDraw = panda::MeshDraw {};
        unsampledMrDraw.mesh = meshes->cube;
        unsampledMrDraw.material.metallicRoughnessTexture = *unsampledMetallicRoughnessMap;
        unsampledMrView.meshes.push_back(unsampledMrDraw);
        const auto unsampledMrResult = context.render(unsampledMrView);
        ASSERT_FALSE(unsampledMrResult.has_value());
        EXPECT_EQ(unsampledMrResult.error().code, panda::ErrorCode::InvalidArgument);

        auto unuploadedMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Srgb,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
        });
        ASSERT_TRUE(unuploadedMap.has_value()) << unuploadedMap.error().message;
        expectInvalidEmissiveMap(context, view->camera, meshes->cube, *unuploadedMap);
        auto unuploadedMapView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto unuploadedBaseColorDraw = panda::MeshDraw {};
        unuploadedBaseColorDraw.mesh = meshes->cube;
        unuploadedBaseColorDraw.material.baseColorTexture = *unuploadedMap;
        unuploadedMapView.meshes.push_back(unuploadedBaseColorDraw);
        const auto unuploadedMapResult = context.render(unuploadedMapView);
        ASSERT_FALSE(unuploadedMapResult.has_value());
        EXPECT_EQ(unuploadedMapResult.error().code, panda::ErrorCode::InvalidArgument);

        auto unuploadedMrMap = context.createTexture({
            .extent = vk::Extent2D {1, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
        });
        ASSERT_TRUE(unuploadedMrMap.has_value()) << unuploadedMrMap.error().message;
        expectInvalidOcclusionMap(context, view->camera, meshes->cube, *unuploadedMrMap);
        auto unuploadedMrView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto unuploadedMrDraw = panda::MeshDraw {};
        unuploadedMrDraw.mesh = meshes->cube;
        unuploadedMrDraw.material.metallicRoughnessTexture = *unuploadedMrMap;
        unuploadedMrView.meshes.push_back(unuploadedMrDraw);
        const auto unuploadedMrResult = context.render(unuploadedMrView);
        ASSERT_FALSE(unuploadedMrResult.has_value());
        EXPECT_EQ(unuploadedMrResult.error().code, panda::ErrorCode::InvalidArgument);

        auto negativeEmissiveView = panda::RenderView {.camera = view->camera, .directionalLight = {}, .meshes = {}};
        auto negativeEmissiveDraw = panda::MeshDraw {.mesh = meshes->cube};
        negativeEmissiveDraw.material.emissive = {-1.0F, 0.0F, 0.0F};
        negativeEmissiveView.meshes.push_back(negativeEmissiveDraw);
        const auto negativeEmissiveResult = context.render(negativeEmissiveView);
        ASSERT_FALSE(negativeEmissiveResult.has_value());
        EXPECT_EQ(negativeEmissiveResult.error().code, panda::ErrorCode::InvalidArgument);
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

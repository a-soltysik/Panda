#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <glm/trigonometric.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Cube.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/RenderView.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowSurface.hpp>
#include <panda/tools/Camera.hpp>
#include <panda/tools/gui/Window.hpp>
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

auto createUploadedCube(const panda::Context& context) -> panda::Result<panda::Mesh>
{
    const auto geometry = panda::createCubeGeometry();
    auto mesh = context.createMeshFromGeometry(geometry);
    if (!mesh)
    {
        return std::unexpected {mesh.error()};
    }
    auto batch = context.createUploadBatch();
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
    const auto waited = context.wait(*completion, std::chrono::seconds {10});
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (*waited != panda::WaitStatus::Completed)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::Timeout, "Cube upload did not complete")};
    }
    return std::move(*mesh);
}

auto makeCubeView(panda::FramebufferExtent extent, panda::Mesh mesh) -> panda::Result<panda::RenderView>
{
    const auto aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const auto camera = panda::tools::createPerspectiveCamera({
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
    view.meshes.push_back({.mesh = std::move(mesh)});
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

TEST(WindowedContextSystem, RendersUploadedCubeWithValidation)
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
        auto mesh = createUploadedCube(context);
        ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
        const auto extent = window->getFramebufferExtent();
        ASSERT_TRUE(extent.has_value()) << extent.error().message;
        auto view = makeCubeView(*extent, std::move(*mesh));
        ASSERT_TRUE(view.has_value()) << view.error().message;
        const auto frame = context.render(*view);
        ASSERT_TRUE(frame.has_value()) << frame.error().message;
        EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
        view->meshes.clear();
        verifyCompletion(context, frame->completion);
    }
    const auto validationError = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(validationError, records.entries.end())
        << (validationError == records.entries.end() ? "" : validationError->message);
}

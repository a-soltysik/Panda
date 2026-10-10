#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <expected>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <panda/tools/gui/Window.hpp>
#include <string>
#include <utility>
#include <vector>

#include "GlfwTestSupport.hpp"

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

    [[nodiscard]] auto createSurface(VkInstance instance) const -> std::expected<VkSurfaceKHR, panda::Error> override
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

    [[nodiscard]] auto createSurface([[maybe_unused]] VkInstance instance) const
        -> std::expected<VkSurfaceKHR, panda::Error> override
    {
        if (_returnNull)
        {
            return VkSurfaceKHR {};
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
    auto movedContext = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_NE(movedContext.getDeviceInfo().swapchainMaintenance, panda::ContextDeviceInfo::SwapchainMaintenance::None);
    for (auto index = 0; index < 3; ++index)
    {
        ASSERT_TRUE(window->processInput().has_value());
        const auto frame = movedContext.presentClearFrame();
        ASSERT_TRUE(frame.has_value()) << frame.error().message;
        EXPECT_EQ(frame->status, panda::FrameResult::Status::Presented);
        EXPECT_NE(frame->completionValue, 0U);
    }
    ASSERT_TRUE(window->setSize({240, 180}).has_value());
    ASSERT_TRUE(window->processInput().has_value());
    const auto resized = movedContext.presentClearFrame();
    ASSERT_TRUE(resized.has_value()) << resized.error().message;
    EXPECT_EQ(resized->status, panda::FrameResult::Status::Presented);
    EXPECT_NE(resized->completionValue, 0U);

    // Native iconification is asynchronous and may be ignored by a window manager.
    // The surface reports the same zero framebuffer state deterministically.
    surface.setSuspended(true);
    const auto suspended = movedContext.presentClearFrame();
    ASSERT_TRUE(suspended.has_value());
    EXPECT_EQ(suspended->status, panda::FrameResult::Status::Suspended);
    EXPECT_EQ(suspended->completionValue, 0U);

    surface.setSuspended(false);
    const auto restored = movedContext.presentClearFrame();
    ASSERT_TRUE(restored.has_value()) << restored.error().message;
    EXPECT_EQ(restored->status, panda::FrameResult::Status::Presented);
    EXPECT_NE(restored->completionValue, 0U);
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

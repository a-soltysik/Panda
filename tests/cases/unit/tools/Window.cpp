#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <glm/ext/vector_uint2.hpp>
#include <limits>
#include <panda/Error.hpp>
#include <panda/tools/gui/Window.hpp>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "external/glfw/GlfwMock.hpp"

namespace
{
using panda::tools::Window;
using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::NotNull;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
constexpr glm::uvec2 size {100, 80};

void previousCallback([[maybe_unused]] int code, [[maybe_unused]] const char* description) { }

class WindowTest : public testing::Test
{
protected:
    WindowTest();

    void expectInitialization(GLFWerrorfun previous = nullptr);

    void expectCreation(GLFWwindow* handle, const char* title = "test");

    void expectDestruction(GLFWwindow* handle);

    void expectSessionRelease(GLFWerrorfun previous = nullptr);

    void expectError(int code, const char* description);

    auto getFirst() -> GLFWwindow*;

    auto getSecond() -> GLFWwindow*;

    panda::test::ScopedMock<panda::test::GlfwMock> _glfw;

private:
    testing::Sequence _cleanup;
    GLFWwindow _first;
    GLFWwindow _second;
};

WindowTest::WindowTest()
{
    // Error clearing and successful status reads are plumbing, not simulated GLFW state.
    EXPECT_CALL(_glfw, glfwGetError(nullptr)).WillRepeatedly(Return(GLFW_NO_ERROR));
    EXPECT_CALL(_glfw, glfwGetError(NotNull())).WillRepeatedly(DoAll(SetArgPointee<0>(nullptr), Return(GLFW_NO_ERROR)));
}

auto WindowTest::getFirst() -> GLFWwindow*
{
    return &_first;
}

auto WindowTest::getSecond() -> GLFWwindow*
{
    return &_second;
}

void WindowTest::expectInitialization(GLFWerrorfun previous)
{
    // Reusable actions avoid an MSVC STL analyzer false positive in gMock's OnceAction.
    EXPECT_CALL(_glfw, glfwSetErrorCallback(NotNull())).Times(1).WillRepeatedly(Return(previous));
    EXPECT_CALL(_glfw, glfwInit()).Times(1).WillRepeatedly(Return(GLFW_TRUE));
}

void WindowTest::expectCreation(GLFWwindow* handle, const char* title)
{
    EXPECT_CALL(_glfw, glfwDefaultWindowHints()).RetiresOnSaturation();
    EXPECT_CALL(_glfw, glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)).RetiresOnSaturation();
    EXPECT_CALL(_glfw, glfwCreateWindow(100, 80, StrEq(title), nullptr, nullptr))
        .Times(1)
        .WillRepeatedly(Return(handle));
}

void WindowTest::expectDestruction(GLFWwindow* handle)
{
    EXPECT_CALL(_glfw, glfwDestroyWindow(handle)).InSequence(_cleanup);
}

void WindowTest::expectSessionRelease(GLFWerrorfun previous)
{
    EXPECT_CALL(_glfw, glfwTerminate()).InSequence(_cleanup);
    EXPECT_CALL(_glfw, glfwSetErrorCallback(previous)).Times(1).InSequence(_cleanup).WillRepeatedly(Return(nullptr));
}

void WindowTest::expectError(int code, const char* description)
{
    EXPECT_CALL(_glfw, glfwGetError(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(description), Return(code)))
        .RetiresOnSaturation();
}
}

TEST_F(WindowTest, ValidatesRequestsBeforeCallingGlfw)
{
    EXPECT_CALL(_glfw, glfwInit()).Times(0);
    EXPECT_CALL(_glfw, glfwCreateWindow(_, _, _, _, _)).Times(0);
    const auto zero = Window::create({0, 80}, "test");
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(Window::create({std::numeric_limits<std::uint32_t>::max(), 80}, "test").has_value());
    EXPECT_FALSE(Window::create(size, nullptr).has_value());
}

TEST_F(WindowTest, InitializationFailureOwnsDiagnosticAndRestoresCallback)
{
    EXPECT_CALL(_glfw, glfwSetErrorCallback(NotNull())).WillOnce(Return(previousCallback));
    EXPECT_CALL(_glfw, glfwInit()).WillOnce(Return(GLFW_FALSE));
    EXPECT_CALL(_glfw, glfwTerminate()).Times(0);
    EXPECT_CALL(_glfw, glfwSetErrorCallback(previousCallback)).WillOnce(Return(nullptr));
    auto description = std::string {"Initialization failed"};
    expectError(GLFW_PLATFORM_UNAVAILABLE, description.c_str());
    const auto failure = Window::create(size, "test");
    ASSERT_FALSE(failure.has_value());
    description = "Changed backend text";
    EXPECT_NE(failure.error().message, description);
    EXPECT_EQ(failure.error().code, panda::ErrorCode::Unsupported);
    ASSERT_TRUE(failure.error().native.has_value());
    const auto native = failure.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "GLFW");
    EXPECT_EQ(native.code, GLFW_PLATFORM_UNAVAILABLE);
    EXPECT_THAT(failure.error().source.function_name(), HasSubstr("Window::create"));
    EXPECT_EQ(failure.error().message, "Initialization failed");
    ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(&_glfw));

    expectInitialization();
    expectCreation(getFirst(), "retry");
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(_glfw, glfwGetError(_)).WillRepeatedly(Return(GLFW_NO_ERROR));
    const auto retry = Window::create(size, "retry");
    EXPECT_TRUE(retry.has_value());
}

TEST_F(WindowTest, CreationFailureReleasesSessionAndAllowsRetry)
{
    expectInitialization();
    expectCreation(nullptr);
    expectSessionRelease();
    // The first status read checks hints; the second reads the creation failure.
    EXPECT_CALL(_glfw, glfwGetError(NotNull()))
        .WillOnce(Return(GLFW_NO_ERROR))
        .WillOnce(DoAll(SetArgPointee<0>("Creation failed"), Return(GLFW_PLATFORM_ERROR)))
        .RetiresOnSaturation();
    const auto failure = Window::create(size, "test");
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(failure.error().native.has_value());
    const auto native = failure.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, GLFW_PLATFORM_ERROR);
    EXPECT_THAT(failure.error().source.function_name(), HasSubstr("Window::create"));
    expectInitialization();
    expectCreation(getFirst(), "retry");
    expectDestruction(getFirst());
    expectSessionRelease();
    const auto retry = Window::create(size, "retry");
    EXPECT_TRUE(retry.has_value());
}

TEST_F(WindowTest, MultipleWindowsShareSessionAndMoveWithoutDoubleDestruction)
{
    expectInitialization(previousCallback);
    expectCreation(getFirst(), "first");
    expectCreation(getSecond(), "second");
    expectCreation(nullptr, "failed third");
    expectDestruction(getSecond());
    expectDestruction(getFirst());
    expectSessionRelease(previousCallback);
    EXPECT_CALL(_glfw, glfwWindowShouldClose(getFirst())).WillOnce(Return(GLFW_FALSE));
    auto first = Window::create(size, "first");
    ASSERT_TRUE(first.has_value());
    const auto identifier = first->getId();
    const auto moved = std::move(*first);
    EXPECT_FALSE(first->isValid());
    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.getId(), identifier);
    {
        const auto second = Window::create(size, "second");
        ASSERT_TRUE(second.has_value());
        EXPECT_NE(second->getId(), identifier);
        EXPECT_CALL(_glfw, glfwGetError(NotNull()))
            .WillOnce(Return(GLFW_NO_ERROR))
            .WillOnce(DoAll(SetArgPointee<0>("Creation failed"), Return(GLFW_PLATFORM_ERROR)))
            .RetiresOnSaturation();
        EXPECT_FALSE(Window::create(size, "failed third").has_value());
    }
    EXPECT_EQ(moved.shouldClose(), false);
}

TEST_F(WindowTest, QueriesCurrentDimensionsMinimizationAndCloseFlag)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(_glfw, glfwGetWindowSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(160), SetArgPointee<2>(80)));
    EXPECT_CALL(_glfw, glfwGetFramebufferSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)));
    EXPECT_CALL(_glfw, glfwGetWindowAttrib(getFirst(), GLFW_ICONIFIED))
        .WillOnce(Return(GLFW_FALSE))
        .WillOnce(Return(GLFW_FALSE))
        .WillOnce(Return(GLFW_TRUE));
    EXPECT_CALL(_glfw, glfwWindowShouldClose(getFirst())).WillOnce(Return(GLFW_TRUE));
    const auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->getSize(), size);
    EXPECT_EQ(window->getSize(), (glm::uvec2 {160, 80}));
    EXPECT_EQ(window->isMinimized(), false);
    EXPECT_EQ(window->isMinimized(), true);
    EXPECT_EQ(window->isMinimized(), true);
    EXPECT_EQ(window->shouldClose(), true);
}

TEST_F(WindowTest, ResizeAndMinimizeReportBackendFailures)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());

    EXPECT_CALL(_glfw, glfwSetWindowSize(getFirst(), 160, 120)).Times(2);
    const auto invalid = window->setSize({0, 80});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);

    expectError(GLFW_PLATFORM_ERROR, "Resize failed");
    const auto failed = window->setSize({160, 120});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(failed.error().native.has_value());
    const auto native = failed.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, GLFW_PLATFORM_ERROR);
    EXPECT_THAT(failed.error().source.function_name(), HasSubstr("Window::setSize"));
    EXPECT_TRUE(window->setSize({160, 120}).has_value());

    EXPECT_CALL(_glfw, glfwIconifyWindow(getFirst()));
    EXPECT_CALL(_glfw, glfwRestoreWindow(getFirst()));
    EXPECT_TRUE(window->setMinimized(true).has_value());
    EXPECT_TRUE(window->setMinimized(false).has_value());
}

TEST_F(WindowTest, CopiesRequiredExtensionsAndRejectsMissingSurfaceInputs)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());

    auto extension = std::string {"VK_KHR_surface"};
    auto names = std::array {extension.c_str()};
    EXPECT_CALL(_glfw, glfwGetRequiredInstanceExtensions(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(1U), Return(names.data())))
        .WillOnce(Return(nullptr));
    const auto copied = window->getRequiredInstanceExtensions();
    ASSERT_TRUE(copied.has_value());
    extension.clear();
    EXPECT_EQ(*copied, (std::vector<std::string> {"VK_KHR_surface"}));

    expectError(GLFW_API_UNAVAILABLE, "Vulkan unavailable");
    const auto missing = window->getRequiredInstanceExtensions();
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, panda::ErrorCode::Unsupported);
    ASSERT_TRUE(missing.error().native.has_value());
    const auto native = missing.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, GLFW_API_UNAVAILABLE);
    EXPECT_THAT(missing.error().source.function_name(), HasSubstr("Window::getRequiredInstanceExtensions"));

    EXPECT_CALL(_glfw, glfwCreateWindowSurface(_, _, _, _)).Times(0);
    const auto nullInstance = window->createSurface(VkInstance {});
    ASSERT_FALSE(nullInstance.has_value());
    EXPECT_EQ(nullInstance.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(WindowTest, QueryErrorsRemainDistinctFromZeroExtent)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    EXPECT_CALL(_glfw, glfwGetWindowSize(getFirst(), _, _));
    expectError(GLFW_PLATFORM_ERROR, "Size query failed");
    const auto dimensions = window->getSize();
    ASSERT_FALSE(dimensions.has_value());
    EXPECT_EQ(dimensions.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(dimensions.error().native.has_value());
    const auto native = dimensions.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, GLFW_PLATFORM_ERROR);
    EXPECT_THAT(dimensions.error().source.function_name(), HasSubstr("Window::getSize"));
    EXPECT_CALL(_glfw, glfwGetFramebufferSize(getFirst(), _, _));
    expectError(GLFW_PLATFORM_ERROR, "Framebuffer query failed");
    const auto minimized = window->isMinimized();
    ASSERT_FALSE(minimized.has_value());
    EXPECT_EQ(minimized.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(WindowTest, NegativeDimensionsAreRecoverableBackendErrors)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(_glfw, glfwGetWindowSize(getFirst(), _, _)).WillOnce(DoAll(SetArgPointee<1>(-1), SetArgPointee<2>(80)));
    EXPECT_CALL(_glfw, glfwGetFramebufferSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(-1)));
    const auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    const auto dimensions = window->getSize();
    ASSERT_FALSE(dimensions.has_value());
    EXPECT_EQ(dimensions.error().message, "GLFW reported negative window dimensions");
    const auto minimized = window->isMinimized();
    ASSERT_FALSE(minimized.has_value());
    EXPECT_EQ(minimized.error().message, "GLFW reported negative framebuffer dimensions");
}

TEST_F(WindowTest, EventFailuresAreRecoverableAndPermitRetry)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(_glfw, glfwPollEvents()).Times(2);
    EXPECT_CALL(_glfw, glfwWaitEvents()).Times(2);
    auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    expectError(GLFW_PLATFORM_ERROR, "Poll failed");
    EXPECT_FALSE(window->processInput().has_value());
    expectError(GLFW_PLATFORM_ERROR, "Wait failed");
    EXPECT_FALSE(Window::waitForInput().has_value());
    EXPECT_TRUE(window->processInput().has_value());
    EXPECT_TRUE(Window::waitForInput().has_value());
}

using WindowDeathTest = WindowTest;

TEST_F(WindowDeathTest, RejectsMovedFromUse)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    ASSERT_DEATH(
        {
            const auto owner = std::move(*window);
            std::ignore = window->getSize();
        },
        "Window.cpp:.*Cannot use a moved-from Window");
}

TEST_F(WindowDeathTest, RejectsUseFromAnotherThread)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    const auto window = Window::create(size, "test");
    ASSERT_TRUE(window.has_value());
    ASSERT_DEATH(
        {
            const auto worker = std::jthread {[&window] {
                std::ignore = window->getId();
            }};
        },
        "GlfwSession.cpp:.*Window operation requires the owning main thread");
}

TEST_F(WindowDeathTest, RejectsWaitingWithoutLiveWindow)
{
    ASSERT_DEATH(std::ignore = Window::waitForInput(), "Window.cpp:.*Waiting for input requires a live Panda window");
}

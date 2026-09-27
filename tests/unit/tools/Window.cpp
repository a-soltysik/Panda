#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <glm/ext/vector_uint2.hpp>
#include <limits>
#include <panda/tools/gui/Window.hpp>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

#include "MockTest.hpp"
#include "external/glfw/GlfwMock.hpp"

namespace
{
using panda::tools::Window;
using panda::tools::WindowError;
using testing::_;
using testing::DoAll;
using testing::NotNull;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
constexpr glm::uvec2 size {100, 80};

void previousCallback([[maybe_unused]] int code, [[maybe_unused]] const char* description) { }

class WindowTest : public panda::test::MockTest<panda::test::GlfwMock>
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

private:
    testing::Sequence _cleanup;
    GLFWwindow _first;
    GLFWwindow _second;
};

WindowTest::WindowTest()
{
    // Error clearing and successful status reads are plumbing, not simulated GLFW state.
    EXPECT_CALL(getMock(), glfwGetError(nullptr)).WillRepeatedly(Return(GLFW_NO_ERROR));
    EXPECT_CALL(getMock(), glfwGetError(NotNull()))
        .WillRepeatedly(DoAll(SetArgPointee<0>(nullptr), Return(GLFW_NO_ERROR)));
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
    EXPECT_CALL(getMock(), glfwSetErrorCallback(NotNull())).Times(1).WillRepeatedly(Return(previous));
    EXPECT_CALL(getMock(), glfwInit()).Times(1).WillRepeatedly(Return(GLFW_TRUE));
}

void WindowTest::expectCreation(GLFWwindow* handle, const char* title)
{
    EXPECT_CALL(getMock(), glfwDefaultWindowHints()).RetiresOnSaturation();
    EXPECT_CALL(getMock(), glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)).RetiresOnSaturation();
    EXPECT_CALL(getMock(), glfwCreateWindow(100, 80, StrEq(title), nullptr, nullptr))
        .Times(1)
        .WillRepeatedly(Return(handle));
}

void WindowTest::expectDestruction(GLFWwindow* handle)
{
    EXPECT_CALL(getMock(), glfwDestroyWindow(handle)).InSequence(_cleanup);
}

void WindowTest::expectSessionRelease(GLFWerrorfun previous)
{
    EXPECT_CALL(getMock(), glfwTerminate()).InSequence(_cleanup);
    EXPECT_CALL(getMock(), glfwSetErrorCallback(previous))
        .Times(1)
        .InSequence(_cleanup)
        .WillRepeatedly(Return(nullptr));
}

void WindowTest::expectError(int code, const char* description)
{
    EXPECT_CALL(getMock(), glfwGetError(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(description), Return(code)))
        .RetiresOnSaturation();
}
}

TEST_F(WindowTest, ValidatesRequestsBeforeCallingGlfw)
{
    EXPECT_CALL(getMock(), glfwInit()).Times(0);
    EXPECT_CALL(getMock(), glfwCreateWindow(_, _, _, _, _)).Times(0);
    const auto zero {Window::create({0, 80}, "test")};
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, WindowError::Code::InvalidArgument);
    EXPECT_FALSE(Window::create({std::numeric_limits<std::uint32_t>::max(), 80}, "test").has_value());
    EXPECT_FALSE(Window::create(size, nullptr).has_value());
}

TEST_F(WindowTest, InitializationFailureOwnsDiagnosticAndRestoresCallback)
{
    EXPECT_CALL(getMock(), glfwSetErrorCallback(NotNull())).WillOnce(Return(previousCallback));
    EXPECT_CALL(getMock(), glfwInit()).WillOnce(Return(GLFW_FALSE));
    EXPECT_CALL(getMock(), glfwTerminate()).Times(0);
    EXPECT_CALL(getMock(), glfwSetErrorCallback(previousCallback)).WillOnce(Return(nullptr));
    std::string description {"Initialization failed"};
    expectError(GLFW_PLATFORM_UNAVAILABLE, description.c_str());
    const auto failure {Window::create(size, "test")};
    ASSERT_FALSE(failure.has_value());
    description = "Changed backend text";
    EXPECT_NE(failure.error().message, description);
    EXPECT_EQ(failure.error().code, WindowError::Code::InitializationFailed);
    EXPECT_EQ(failure.error().operation, "glfwInit");
    EXPECT_EQ(failure.error().nativeCode, GLFW_PLATFORM_UNAVAILABLE);
    EXPECT_EQ(failure.error().message, "Initialization failed");
    ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(&getMock()));

    expectInitialization();
    expectCreation(getFirst(), "retry");
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(getMock(), glfwGetError(_)).WillRepeatedly(Return(GLFW_NO_ERROR));
    const auto retry {Window::create(size, "retry")};
    EXPECT_TRUE(retry.has_value());
}

TEST_F(WindowTest, CreationFailureReleasesSessionAndAllowsRetry)
{
    expectInitialization();
    expectCreation(nullptr);
    expectSessionRelease();
    // The first status read checks hints; the second reads the creation failure.
    EXPECT_CALL(getMock(), glfwGetError(NotNull()))
        .WillOnce(Return(GLFW_NO_ERROR))
        .WillOnce(DoAll(SetArgPointee<0>("Creation failed"), Return(GLFW_PLATFORM_ERROR)))
        .RetiresOnSaturation();
    const auto failure {Window::create(size, "test")};
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, WindowError::Code::CreationFailed);
    EXPECT_EQ(failure.error().nativeCode, GLFW_PLATFORM_ERROR);
    EXPECT_EQ(failure.error().operation, "glfwCreateWindow");
    expectInitialization();
    expectCreation(getFirst(), "retry");
    expectDestruction(getFirst());
    expectSessionRelease();
    const auto retry {Window::create(size, "retry")};
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
    EXPECT_CALL(getMock(), glfwWindowShouldClose(getFirst())).WillOnce(Return(GLFW_FALSE));
    auto first {Window::create(size, "first")};
    ASSERT_TRUE(first.has_value());
    const auto identifier {first->getId()};
    const auto moved {std::move(*first)};
    EXPECT_FALSE(first->isValid());
    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.getId(), identifier);
    {
        const auto second {Window::create(size, "second")};
        ASSERT_TRUE(second.has_value());
        EXPECT_NE(second->getId(), identifier);
        EXPECT_CALL(getMock(), glfwGetError(NotNull()))
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
    EXPECT_CALL(getMock(), glfwGetWindowSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(160), SetArgPointee<2>(80)));
    EXPECT_CALL(getMock(), glfwGetFramebufferSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(80)))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(80)));
    EXPECT_CALL(getMock(), glfwGetWindowAttrib(getFirst(), GLFW_ICONIFIED))
        .WillOnce(Return(GLFW_FALSE))
        .WillOnce(Return(GLFW_FALSE))
        .WillOnce(Return(GLFW_TRUE));
    EXPECT_CALL(getMock(), glfwWindowShouldClose(getFirst())).WillOnce(Return(GLFW_TRUE));
    const auto window {Window::create(size, "test")};
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->getSize(), size);
    EXPECT_EQ(window->getSize(), (glm::uvec2 {160, 80}));
    EXPECT_EQ(window->isMinimized(), false);
    EXPECT_EQ(window->isMinimized(), true);
    EXPECT_EQ(window->isMinimized(), true);
    EXPECT_EQ(window->shouldClose(), true);
}

TEST_F(WindowTest, QueryErrorsRemainDistinctFromZeroExtent)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    auto window {Window::create(size, "test")};
    ASSERT_TRUE(window.has_value());
    EXPECT_CALL(getMock(), glfwGetWindowSize(getFirst(), _, _));
    expectError(GLFW_PLATFORM_ERROR, "Size query failed");
    const auto dimensions {window->getSize()};
    ASSERT_FALSE(dimensions.has_value());
    EXPECT_EQ(dimensions.error().operation, "glfwGetWindowSize");
    EXPECT_EQ(dimensions.error().nativeCode, GLFW_PLATFORM_ERROR);
    EXPECT_CALL(getMock(), glfwGetFramebufferSize(getFirst(), _, _));
    expectError(GLFW_PLATFORM_ERROR, "Framebuffer query failed");
    const auto minimized {window->isMinimized()};
    ASSERT_FALSE(minimized.has_value());
    EXPECT_EQ(minimized.error().operation, "glfwGetFramebufferSize");
}

TEST_F(WindowTest, NegativeDimensionsAreRecoverableBackendErrors)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(getMock(), glfwGetWindowSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(-1), SetArgPointee<2>(80)));
    EXPECT_CALL(getMock(), glfwGetFramebufferSize(getFirst(), _, _))
        .WillOnce(DoAll(SetArgPointee<1>(100), SetArgPointee<2>(-1)));
    const auto window {Window::create(size, "test")};
    ASSERT_TRUE(window.has_value());
    const auto dimensions {window->getSize()};
    ASSERT_FALSE(dimensions.has_value());
    EXPECT_EQ(dimensions.error().message, "GLFW reported negative window dimensions");
    const auto minimized {window->isMinimized()};
    ASSERT_FALSE(minimized.has_value());
    EXPECT_EQ(minimized.error().message, "GLFW reported negative framebuffer dimensions");
}

TEST_F(WindowTest, EventFailuresAreRecoverableAndPermitRetry)
{
    expectInitialization();
    expectCreation(getFirst());
    expectDestruction(getFirst());
    expectSessionRelease();
    EXPECT_CALL(getMock(), glfwPollEvents()).Times(2);
    EXPECT_CALL(getMock(), glfwWaitEvents()).Times(2);
    auto window {Window::create(size, "test")};
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
    auto window {Window::create(size, "test")};
    ASSERT_TRUE(window.has_value());
    ASSERT_DEATH(
        {
            const auto owner {std::move(*window)};
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
    const auto window {Window::create(size, "test")};
    ASSERT_TRUE(window.has_value());
    ASSERT_DEATH(
        {
            const std::jthread worker {[&window] {
                std::ignore = window->getId();
            }};
        },
        "GlfwSession.cpp:.*Window operation requires the owning main thread");
}

TEST_F(WindowDeathTest, RejectsWaitingWithoutLiveWindow)
{
    ASSERT_DEATH(std::ignore = Window::waitForInput(), "Window.cpp:.*Waiting for input requires a live Panda window");
}

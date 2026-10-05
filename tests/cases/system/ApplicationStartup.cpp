#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <panda/tools/gui/Window.hpp>

TEST(ApplicationStartupSystem, NativeStartupEventsAndTeardown)
{
    auto window = panda::tools::Window::create({160, 120}, "Panda native smoke");
    if (!window && window.error().native && window.error().native->api == "GLFW" &&
        (window.error().native->code == GLFW_PLATFORM_UNAVAILABLE ||
         window.error().native->code == GLFW_PLATFORM_ERROR))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    EXPECT_TRUE(window->isValid());
    ASSERT_TRUE(window->processInput().has_value());
    EXPECT_EQ(window->shouldClose(), false);
}

#include <gtest/gtest.h>

#include <panda/tools/gui/Window.hpp>

#include "GlfwTestSupport.hpp"

TEST(ApplicationStartupSystem, NativeStartupEventsAndTeardown)
{
    auto window = panda::tools::Window::create({160, 120}, "Panda native smoke");
    if (!window && panda::test::isGlfwPlatformUnavailable(window.error()))
    {
        GTEST_SKIP() << "Native window platform unavailable: " << window.error().message;
    }
    ASSERT_TRUE(window.has_value());
    EXPECT_TRUE(window->isValid());
    ASSERT_TRUE(window->processInput().has_value());
    EXPECT_EQ(window->shouldClose(), false);
}

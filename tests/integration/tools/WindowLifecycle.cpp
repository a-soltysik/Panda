#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <glm/ext/vector_uint2.hpp>
#include <panda/tools/gui/Window.hpp>
#include <utility>

TEST(WindowLifecycleIntegration, NullPlatformSharesSessionAndPreservesOwnership)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    auto first {panda::tools::Window::create({160, 120}, "Panda window integration")};
    ASSERT_TRUE(first.has_value());
    {
        auto second {panda::tools::Window::create({100, 80}, "Panda second window")};
        ASSERT_TRUE(second.has_value());
        const auto identity {second->getId()};
        auto moved {std::move(*second)};
        EXPECT_FALSE(second->isValid());
        EXPECT_EQ(moved.getId(), identity);
        ASSERT_TRUE(moved.processInput().has_value());
    }
    ASSERT_TRUE(first->processInput().has_value());
    EXPECT_EQ(first->getSize(), (glm::uvec2 {160, 120}));
    EXPECT_EQ(first->shouldClose(), false);
}

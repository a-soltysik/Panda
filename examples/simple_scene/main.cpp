#ifdef __linux__
#    define GLFW_INCLUDE_NONE
#    include <GLFW/glfw3.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <glm/ext/vector_uint2.hpp>
#include <panda/Logger.hpp>
#include <panda/tools/gui/Window.hpp>
#include <tuple>

auto main() -> int
{
    try
    {
#ifdef __linux__
        // An empty Wayland surface is not mapped until the application presents a buffer.
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        static constexpr glm::uvec2 windowSize {1280, 720};
        auto window {panda::tools::Window::create(windowSize, "Panda - Simple scene")};
        if (!window)
        {
            const auto& error {window.error()};
            panda::log::error("{} failed ({}): {}", error.operation, error.nativeCode, error.message);
            return EXIT_FAILURE;
        }

        panda::log::info("Application loop started");
        while (!window->shouldClose())
        {
            if (auto result {panda::tools::Window::waitForInput()}; !result)
            {
                const auto& error {result.error()};
                panda::log::error("{} failed ({}): {}", error.operation, error.nativeCode, error.message);
                return EXIT_FAILURE;
            }
        }
        panda::log::info("Application loop stopped");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::ignore = std::fputs("Unexpected application failure: ", stderr);
        std::ignore = std::fputs(error.what(), stderr);
        std::ignore = std::fputc('\n', stderr);
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::ignore = std::fputs("Unexpected application failure\n", stderr);
        return EXIT_FAILURE;
    }
}

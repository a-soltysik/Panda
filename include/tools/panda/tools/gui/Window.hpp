#pragma once

/// @file
/// GLFW window ownership and event processing.

#include <cstdint>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <memory>
#include <string>

struct GLFWwindow;

namespace panda::tools
{
class GlfwSession;

/// @brief Owned window diagnostic; nativeCode is a GLFW error code or zero.
struct WindowError
{
    /// @brief Failure category independent of the native GLFW status.
    enum class Code : std::uint8_t
    {
        InvalidArgument,
        InitializationFailed,
        CreationFailed,
        OperationFailed
    };

    /// Failure category.
    Code code {Code::InvalidArgument};
    /// Operation that failed, owned by the error.
    std::string operation;
    /// Diagnostic text, owned by the error.
    std::string message;
    /// GLFW error code, or zero when no native status applies.
    int nativeCode {0};
};

/// @brief Move-only owner of a GLFW window without an OpenGL context.
/// Operations, moves and destruction require the main thread, outside callbacks.
/// Panda exclusively manages GLFW initialization, termination and its error callback
/// while windows exist. Do not independently manage GLFW during that time.
class Window final
{
public:
    /// @brief Non-owning native window identity, valid only for a live Window.
    using Id = std::uintptr_t;

    /// @brief Creates a window and shares the GLFW session, or returns an owned error.
    /// @param size Positive content dimensions in screen coordinates, each at most INT_MAX.
    /// @param name Non-null UTF-8, null-terminated title, copied by GLFW before return.
    /// @return A valid owner or validation/initialization/creation failure. May allocate and block.
    [[nodiscard]] static auto create(glm::uvec2 size, const char* name) -> std::expected<Window, WindowError>;

    Window(const Window&) = delete;

    auto operator=(const Window&) -> Window& = delete;

    /// @brief Transfers ownership; the source may only be destroyed or queried with isValid().
    Window(Window&& other) noexcept;

    auto operator=(Window&&) -> Window& = delete;

    /// @brief Destroys this window; the last owner terminates GLFW and restores its callback.
    ~Window() noexcept;

    /// @brief Reports ownership; safe on a moved-from object.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Returns the close-request flag; does not destroy the window.
    /// @return The flag. Moved-from use, wrong-thread use or lost GLFW initialization
    /// is a fatal lifetime-contract violation rather than a recoverable operation error.
    [[nodiscard]] auto shouldClose() const -> bool;

    /// @brief Reports iconification or a zero-sized framebuffer.
    /// @return The state or a backend error, including negative dimensions;
    /// failure is distinct from minimization.
    [[nodiscard]] auto isMinimized() const -> std::expected<bool, WindowError>;

    /// @brief Queries current content dimensions in screen coordinates, not framebuffer pixels.
    /// @return Dimensions or a backend error, including negative dimensions;
    /// failure is distinct from a zero extent.
    [[nodiscard]] auto getSize() const -> std::expected<glm::uvec2, WindowError>;

    /// @brief Non-owning identity valid for this window's lifetime; may be reused later.
    [[nodiscard]] auto getId() const -> Id;

    /// @brief Polls events for every GLFW window; callbacks run before return.
    /// @return Success or a GLFW event-processing error.
    [[nodiscard]] auto processInput() -> std::expected<void, WindowError>;

    /// @brief Waits for events; requires a live Panda window and may block indefinitely.
    /// @return Success or a GLFW event-processing error.
    [[nodiscard]] static auto waitForInput() -> std::expected<void, WindowError>;

private:
    Window(GLFWwindow* window, std::shared_ptr<GlfwSession> session) noexcept;

    void checkUsable() const;

    GLFWwindow* _window {nullptr};
    std::shared_ptr<GlfwSession> _session;
};

}

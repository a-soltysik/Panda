#pragma once

/// @file
/// Window-system boundary used by the Vulkan context.

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <panda/Error.hpp>
#include <string>
#include <vector>

namespace panda
{

/// @brief Current framebuffer dimensions in pixels; zero means rendering is suspended.
struct FramebufferExtent
{
    /// Framebuffer width in pixels.
    std::uint32_t width {0};
    /// Framebuffer height in pixels.
    std::uint32_t height {0};
};

/// @brief Window-system operations borrowed by a windowed Context.
/// Calls follow the implementation's thread requirements, outside callbacks.
/// The object and its native window must outlive every Context created from it.
class WindowSurface
{
public:
    WindowSurface() = default;
    WindowSurface(const WindowSurface&) = delete;
    auto operator=(const WindowSurface&) -> WindowSurface& = delete;
    WindowSurface(WindowSurface&&) = delete;
    auto operator=(WindowSurface&&) -> WindowSurface& = delete;
    virtual ~WindowSurface() = default;

    /// @brief Lists required Vulkan instance extensions with owned names.
    /// @return Extension names or a window-system failure. The list must include
    /// VK_KHR_surface and the platform surface extension.
    [[nodiscard]] virtual auto getRequiredInstanceExtensions() const -> Result<std::vector<std::string>> = 0;

    /// @brief Creates a surface for a live instance and transfers its ownership.
    /// @param instance Instance supplied by Context; valid only during this call.
    /// @return A surface destroyed by Context or a native failure.
    [[nodiscard]] virtual auto createSurface(VkInstance instance) const -> Result<VkSurfaceKHR> = 0;

    /// @brief Queries the actual framebuffer size, including zero while minimized.
    /// @return Pixel extent or a window-system failure.
    [[nodiscard]] virtual auto getFramebufferExtent() const -> Result<FramebufferExtent> = 0;
};

}

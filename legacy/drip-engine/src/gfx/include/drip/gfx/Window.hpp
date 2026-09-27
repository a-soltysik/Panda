#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <glm/ext/vector_uint2.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace drip::gfx
{

class Window
{
public:
    using Id = size_t;

    Window() = default;
    Window(const Window&) = delete;
    Window(Window&&) = delete;
    auto operator=(const Window&) = delete;
    auto operator=(Window&&) = delete;

    virtual ~Window() noexcept = default;

    [[nodiscard]] virtual auto shouldClose() const -> bool = 0;
    [[nodiscard]] virtual auto isMinimized() const -> bool = 0;
    [[nodiscard]] virtual auto getSize() const -> glm::uvec2 = 0;
    [[nodiscard]] virtual auto getRequiredExtensions() const -> std::vector<const char*> = 0;
    [[nodiscard]] virtual auto createSurface(VkInstance instance) const -> VkSurfaceKHR = 0;
    [[nodiscard]] virtual auto getId() const -> Id = 0;
    virtual void processInput() = 0;
    virtual void waitForInput() = 0;
};

}

#pragma once

#include <vulkan/vulkan_core.h>

#include <glm/fwd.hpp>
#include <vector>

#include "Common.h"

namespace panda
{

class Window
{
public:
    using Id = size_t;

    virtual ~Window() noexcept = default;

    [[nodiscard]] virtual auto shouldClose() const -> bool = 0;
    [[nodiscard]] virtual auto isMinimized() const -> bool = 0;
    [[nodiscard]] virtual auto getSize() const -> glm::uvec2 = 0;
    [[nodiscard]] virtual auto getRequiredExtensions() const -> std::vector<const char*> = 0;
    [[nodiscard]] virtual auto createSurface(VkInstance instance) const -> VkSurfaceKHR = 0;
    [[nodiscard]] virtual auto getId() const -> Id = 0;
    virtual auto processInput() -> void = 0;
    virtual auto waitForInput() -> void = 0;
};

}

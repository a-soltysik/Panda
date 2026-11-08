#pragma once

#include <memory>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>

namespace panda::core::vulkan
{
class ContextResources;
}

namespace panda::core::vulkan::context
{
[[nodiscard]] auto createResources(const WindowSurface* surface, const ContextOptions& options)
    -> Result<std::unique_ptr<ContextResources>>;
}

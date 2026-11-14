#pragma once

#include <memory>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>

namespace panda::core::vulkan
{
class ContextResources;
}

namespace panda::core::vulkan::engine
{
[[nodiscard]] auto createResources(const WindowSurface* surface, const EngineOptions& options)
    -> Result<std::unique_ptr<ContextResources>>;
}

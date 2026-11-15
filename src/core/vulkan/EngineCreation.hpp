#pragma once

#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>

#include "EngineCreateInfo.hpp"

namespace panda::core::vulkan::engine
{
[[nodiscard]] auto createResources(const WindowSurface* surface, const EngineOptions& options)
    -> Result<EngineCreateInfo>;
}

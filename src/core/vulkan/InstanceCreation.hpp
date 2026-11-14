#pragma once

#include <expected>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>

#include "InstanceResources.hpp"

namespace panda::core::vulkan::instance
{
[[nodiscard]] auto createResources(const vk::raii::Context& loader,
                                   const WindowSurface* surface,
                                   const EngineOptions& options) -> Result<InstanceResources>;
}

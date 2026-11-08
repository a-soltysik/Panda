#pragma once

#include <expected>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>

#include "ContextResources.hpp"

namespace panda::core::vulkan::instance
{
[[nodiscard]] auto createResources(const vk::raii::Context& loader,
                                   const WindowSurface* surface,
                                   const ContextOptions& options) -> Result<InstanceResources>;
}

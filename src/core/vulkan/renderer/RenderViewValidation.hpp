#pragma once

#include <panda/Error.hpp>
#include <panda/RenderView.hpp>

#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
[[nodiscard]] auto validateRenderViewData(const RenderView& view) -> Result<void>;
[[nodiscard]] auto validateRenderViewMeshes(const RenderView& view, const resources::MemoryPool& pool) -> Result<void>;
}

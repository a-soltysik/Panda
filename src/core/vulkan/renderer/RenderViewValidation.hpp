#pragma once

#include <cstdint>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>

#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
[[nodiscard]] auto validateRenderViewData(const RenderView& view) -> Result<void>;
[[nodiscard]] auto validateRenderViewLightCount(const RenderView& view, std::uint32_t maxLights) -> Result<void>;
[[nodiscard]] auto validateRenderViewMeshes(const RenderView& view, const resources::MemoryPool& pool) -> Result<void>;
[[nodiscard]] auto validateRenderViewTextures(const RenderView& view) -> Result<void>;
[[nodiscard]] auto validateRenderView(const RenderView& view,
                                      std::uint32_t maxLights,
                                      const resources::MemoryPool& pool) -> Result<void>;
}

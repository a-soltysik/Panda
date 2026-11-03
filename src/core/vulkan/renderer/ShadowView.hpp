#pragma once

#include <expected>
#include <glm/mat4x4.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>

namespace panda::core::vulkan::renderer
{
[[nodiscard]] auto createDirectionalShadowView(const RenderCamera& camera, const DirectionalLight& light)
    -> std::expected<glm::mat4, Error>;
}

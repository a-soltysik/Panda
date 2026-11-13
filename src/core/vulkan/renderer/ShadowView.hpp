#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <glm/mat4x4.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>

namespace panda::core::vulkan::renderer
{
inline constexpr auto pointShadowFaceCount = std::size_t {6};

[[nodiscard]] auto createDirectionalShadowView(const RenderCamera& camera, const DirectionalLight& light)
    -> std::expected<glm::mat4, Error>;
[[nodiscard]] auto createSpotShadowView(const SpotLight& light) -> std::expected<glm::mat4, Error>;
[[nodiscard]] auto createPointShadowViews(const PointLight& light)
    -> std::expected<std::array<glm::mat4, pointShadowFaceCount>, Error>;
}

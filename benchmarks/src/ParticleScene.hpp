#pragma once

#include <cstdint>
#include <expected>
#include <glm/vec2.hpp>
#include <panda/Engine.hpp>
#include <panda/rendering/RenderView.hpp>

#include "SmokeRunBackend.hpp"

namespace panda_bench
{
[[nodiscard]] auto createParticleScene(const panda::Engine& engine, glm::uvec2 extent, std::uint32_t particleCount)
    -> std::expected<panda::RenderView, Failure>;
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/vec2.hpp>
#include <panda/base/Error.hpp>
#include <span>
#include <vector>

namespace panda::detail
{
inline constexpr auto environmentMapFaceCount = std::uint32_t {6};

struct EnvironmentMapSource
{
    std::uint32_t faceSize {0};
    std::array<std::span<const std::uint16_t>, environmentMapFaceCount> faces;
};

struct EnvironmentMapMip
{
    glm::uvec2 extent {};
    std::array<std::vector<std::uint16_t>, environmentMapFaceCount> faces;
};

struct SpecularEnvironmentMipChain
{
    std::vector<EnvironmentMapMip> levels;
};

[[nodiscard]] auto validateEnvironmentFace(std::span<const std::uint16_t> face, std::uint32_t faceSize) -> Result<void>;
[[nodiscard]] auto prefilterSpecularEnvironmentMap(const EnvironmentMapSource& source, std::uint32_t samplesPerTexel)
    -> Result<SpecularEnvironmentMipChain>;
}

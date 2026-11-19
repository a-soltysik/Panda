#pragma once

#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <span>
#include <vector>

namespace panda::core::vulkan::renderer
{
inline constexpr auto shadowAtlasExtent = std::uint32_t {4096};
inline constexpr auto shadowAtlasCellSize = std::uint32_t {256};
inline constexpr auto shadowAtlasGuard = std::uint32_t {4};
inline constexpr auto maxShadowAtlasLayers = std::uint32_t {256};

struct ShadowAtlasTile
{
    std::uint32_t x {};
    std::uint32_t y {};
    std::uint32_t resolution {};
    std::uint32_t layer {};

    friend auto operator==(const ShadowAtlasTile&, const ShadowAtlasTile&) -> bool = default;
};

struct ShadowAtlasView
{
    std::uint32_t lightIndex {};
    std::uint32_t faceIndex {};
    ShadowAtlasTile tile;
    glm::mat4 viewProjection {1.0F};
    float depthBias {};
    float normalOffset {};
};

struct ShadowAtlasPlan
{
    std::vector<ShadowAtlasView> views;
    std::vector<std::uint32_t> firstViewByLight;
    std::vector<std::uint32_t> viewCountByLight;
    std::uint32_t atlasLayerCount {1};
};

[[nodiscard]] auto allocateShadowAtlas(std::span<const std::uint32_t> resolutions)
    -> std::expected<std::vector<ShadowAtlasTile>, Error>;
[[nodiscard]] auto buildShadowAtlasPlan(const RenderView& view) -> std::expected<ShadowAtlasPlan, Error>;
}

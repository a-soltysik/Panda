#include "renderer/ShadowAtlas.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <variant>
#include <vector>

#include "Overloaded.hpp"
#include "renderer/ShadowView.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
constexpr auto atlasCellsPerSide = shadowAtlasExtent / shadowAtlasCellSize;
constexpr auto atlasCellCount = static_cast<std::size_t>(atlasCellsPerSide);
constexpr auto defaultDirectionalShadowResolution = std::uint32_t {2048};
constexpr auto defaultSpotShadowResolution = std::uint32_t {1024};
constexpr auto validResolutions = std::array {256U, 512U, 1024U, 2048U};
using AtlasOccupancy = std::array<std::array<bool, atlasCellCount>, atlasCellCount>;

struct AtlasPosition
{
    std::uint32_t left;
    std::uint32_t top;
};

auto isValidResolution(std::uint32_t resolution) -> bool
{
    return std::ranges::find(validResolutions, resolution) != validResolutions.end();
}

auto occupiedCells(std::uint32_t resolution) -> std::uint32_t
{
    const auto guardedResolution = resolution + (shadowAtlasGuard * 2U);
    return (guardedResolution + shadowAtlasCellSize - 1U) / shadowAtlasCellSize;
}

auto isTileFree(const AtlasOccupancy& occupied, AtlasPosition position, std::uint32_t cellWidth) -> bool
{
    const auto tileCellCount = cellWidth * cellWidth;
    for (auto index = std::uint32_t {}; index < tileCellCount; ++index)
    {
        const auto row = position.top + (index / cellWidth);
        const auto column = position.left + (index % cellWidth);
        if (occupied.at(row).at(column))
        {
            return false;
        }
    }
    return true;
}

void occupyTile(AtlasOccupancy& occupied, AtlasPosition position, std::uint32_t cellWidth)
{
    const auto tileCellCount = cellWidth * cellWidth;
    for (auto index = std::uint32_t {}; index < tileCellCount; ++index)
    {
        const auto row = position.top + (index / cellWidth);
        const auto column = position.left + (index % cellWidth);
        occupied.at(row).at(column) = true;
    }
}

auto findAtlasTile(AtlasOccupancy& occupied, std::uint32_t resolution) -> std::optional<ShadowAtlasTile>
{
    const auto cellWidth = occupiedCells(resolution);
    const auto candidateWidth = atlasCellsPerSide - cellWidth + 1U;
    const auto candidateCount = candidateWidth * candidateWidth;
    for (auto index = std::uint32_t {}; index < candidateCount; ++index)
    {
        const auto position = AtlasPosition {.left = index % candidateWidth, .top = index / candidateWidth};
        if (isTileFree(occupied, position, cellWidth))
        {
            occupyTile(occupied, position, cellWidth);
            return ShadowAtlasTile {.x = (position.left * shadowAtlasCellSize) + shadowAtlasGuard,
                                    .y = (position.top * shadowAtlasCellSize) + shadowAtlasGuard,
                                    .resolution = resolution};
        }
    }
    return std::nullopt;
}

auto appendShadowView(ShadowAtlasPlan& plan, ShadowAtlasView shadowView) -> std::expected<void, Error>
{
    if (plan.views.size() >= std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Shadow view count exceeds shader indices")};
    }
    if (plan.viewCountByLight.at(shadowView.lightIndex) == 0)
    {
        plan.firstViewByLight.at(shadowView.lightIndex) = static_cast<std::uint32_t>(plan.views.size());
    }
    ++plan.viewCountByLight.at(shadowView.lightIndex);
    plan.views.push_back(shadowView);
    return {};
}

auto appendDirectionalShadowViews(ShadowAtlasPlan& plan,
                                  std::size_t lightIndex,
                                  const RenderCamera& camera,
                                  const DirectionalLight& light) -> std::expected<void, Error>
{
    if (!light.shadow)
    {
        return {};
    }
    const auto matrix = createDirectionalShadowView(camera, light);
    if (!matrix)
    {
        return std::unexpected {matrix.error()};
    }
    return appendShadowView(
        plan,
        {.lightIndex = static_cast<std::uint32_t>(lightIndex),
         .faceIndex = 0,
         .tile = {.resolution = light.shadow->resolution.value_or(defaultDirectionalShadowResolution)},
         .viewProjection = *matrix,
         .depthBias = light.shadow->depthBias,
         .normalOffset = light.shadow->normalOffset});
}

auto appendPointShadowViews(ShadowAtlasPlan& plan, std::size_t lightIndex, const PointLight& light)
    -> std::expected<void, Error>
{
    if (!light.shadow)
    {
        return {};
    }
    const auto matrices = createPointShadowViews(light);
    if (!matrices)
    {
        return std::unexpected {matrices.error()};
    }
    const auto resolution = light.shadow->resolution.value_or(512U);
    for (auto faceIndex = std::size_t {}; faceIndex < matrices->size(); ++faceIndex)
    {
        if (auto appended = appendShadowView(plan,
                                             {.lightIndex = static_cast<std::uint32_t>(lightIndex),
                                              .faceIndex = static_cast<std::uint32_t>(faceIndex),
                                              .tile = {.resolution = resolution},
                                              .viewProjection = matrices->at(faceIndex),
                                              .depthBias = light.shadow->depthBias,
                                              .normalOffset = light.shadow->normalOffset});
            !appended)
        {
            return appended;
        }
    }
    return {};
}

auto appendSpotShadowViews(ShadowAtlasPlan& plan, std::size_t lightIndex, const SpotLight& light)
    -> std::expected<void, Error>
{
    if (!light.shadow)
    {
        return {};
    }
    const auto matrix = createSpotShadowView(light);
    if (!matrix)
    {
        return std::unexpected {matrix.error()};
    }
    return appendShadowView(plan,
                            {.lightIndex = static_cast<std::uint32_t>(lightIndex),
                             .faceIndex = 0,
                             .tile = {.resolution = light.shadow->resolution.value_or(defaultSpotShadowResolution)},
                             .viewProjection = *matrix,
                             .depthBias = light.shadow->depthBias,
                             .normalOffset = light.shadow->normalOffset});
}

auto appendLightShadowViews(ShadowAtlasPlan& plan, std::size_t lightIndex, const RenderView& view, const Light& light)
    -> std::expected<void, Error>
{
    return std::visit(
        ::panda::detail::Overloaded {[&](const DirectionalLight& value) {
                                         return appendDirectionalShadowViews(plan, lightIndex, view.camera, value);
                                     },
                                     [&](const PointLight& value) {
                                         return appendPointShadowViews(plan, lightIndex, value);
                                     },
                                     [&](const SpotLight& value) {
                                         return appendSpotShadowViews(plan, lightIndex, value);
                                     }},
        light);
}

auto assignAtlasTiles(ShadowAtlasPlan& plan) -> std::expected<void, Error>
{
    auto resolutions = std::vector<std::uint32_t> {};
    resolutions.reserve(plan.views.size());
    std::ranges::transform(plan.views, std::back_inserter(resolutions), [](const ShadowAtlasView& view) {
        return view.tile.resolution;
    });
    const auto tiles = allocateShadowAtlas(resolutions);
    if (!tiles)
    {
        return std::unexpected {tiles.error()};
    }
    for (auto index = std::size_t {}; index < plan.views.size(); ++index)
    {
        plan.views.at(index).tile = tiles->at(index);
    }
    return {};
}
}

auto allocateShadowAtlas(std::span<const std::uint32_t> resolutions)
    -> std::expected<std::vector<ShadowAtlasTile>, Error>
{
    auto occupied = AtlasOccupancy {};
    auto tiles = std::vector<ShadowAtlasTile> {};
    tiles.reserve(resolutions.size());
    for (const auto resolution : resolutions)
    {
        if (!isValidResolution(resolution))
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument, "Unsupported shadow map resolution")};
        }
        const auto tile = findAtlasTile(occupied, resolution);
        if (!tile)
        {
            return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Shadow maps exceed atlas capacity")};
        }
        tiles.push_back(*tile);
    }
    return tiles;
}

auto buildShadowAtlasPlan(const RenderView& view) -> std::expected<ShadowAtlasPlan, Error>
{
    if (view.lights.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Light count exceeds shadow index range")};
    }
    auto plan = ShadowAtlasPlan {};
    plan.firstViewByLight.resize(view.lights.size(), std::numeric_limits<std::uint32_t>::max());
    plan.viewCountByLight.resize(view.lights.size());
    for (auto lightIndex = std::size_t {}; lightIndex < view.lights.size(); ++lightIndex)
    {
        if (auto appended = appendLightShadowViews(plan, lightIndex, view, view.lights.at(lightIndex)); !appended)
        {
            return std::unexpected {appended.error()};
        }
    }
    if (auto assigned = assignAtlasTiles(plan); !assigned)
    {
        return std::unexpected {assigned.error()};
    }
    return plan;
}
}

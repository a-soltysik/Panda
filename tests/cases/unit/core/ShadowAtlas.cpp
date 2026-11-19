#include "renderer/ShadowAtlas.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/ext/vector_float4.hpp>
#include <limits>
#include <numbers>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <vector>

namespace
{
auto areAtlasTilesSeparated(const panda::core::vulkan::renderer::ShadowAtlasTile& tile,
                            const panda::core::vulkan::renderer::ShadowAtlasTile& other) -> bool
{
    return tile.x + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
               other.x - panda::core::vulkan::renderer::shadowAtlasGuard ||
           other.x + other.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
               tile.x - panda::core::vulkan::renderer::shadowAtlasGuard ||
           tile.y + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
               other.y - panda::core::vulkan::renderer::shadowAtlasGuard ||
           other.y + other.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
               tile.y - panda::core::vulkan::renderer::shadowAtlasGuard;
}

void expectAtlasTileDoesNotOverlapLater(const std::vector<panda::core::vulkan::renderer::ShadowAtlasTile>& tiles,
                                        std::size_t index)
{
    for (auto other = index + 1; other < tiles.size(); ++other)
    {
        const auto& tile = tiles.at(index);
        const auto& next = tiles.at(other);
        if (tile.layer == next.layer)
        {
            EXPECT_TRUE(areAtlasTilesSeparated(tile, next));
        }
    }
}

void expectAtlasTilesDoNotOverlap(const std::vector<panda::core::vulkan::renderer::ShadowAtlasTile>& tiles)
{
    for (auto index = std::size_t {}; index < tiles.size(); ++index)
    {
        expectAtlasTileDoesNotOverlapLater(tiles, index);
    }
}

void expectAtlasTilesValid(const std::vector<panda::core::vulkan::renderer::ShadowAtlasTile>& tiles,
                           const std::array<std::uint32_t, 8>& resolutions)
{
    for (auto index = std::size_t {}; index < tiles.size(); ++index)
    {
        const auto& tile = tiles.at(index);
        EXPECT_EQ(tile.resolution, resolutions.at(index));
        EXPECT_GE(tile.x, panda::core::vulkan::renderer::shadowAtlasGuard);
        EXPECT_GE(tile.y, panda::core::vulkan::renderer::shadowAtlasGuard);
        EXPECT_LE(tile.x + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard,
                  panda::core::vulkan::renderer::shadowAtlasExtent);
        EXPECT_LE(tile.y + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard,
                  panda::core::vulkan::renderer::shadowAtlasExtent);
    }
    expectAtlasTilesDoNotOverlap(tiles);
}
}

TEST(ShadowAtlas, AllocatesRequestedTilesDeterministicallyWithGuards)
{
    static constexpr auto resolutions = std::array {2048U, 1024U, 512U, 512U, 512U, 512U, 512U, 512U};

    const auto first = panda::core::vulkan::renderer::allocateShadowAtlas(resolutions);
    const auto second = panda::core::vulkan::renderer::allocateShadowAtlas(resolutions);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(first->size(), resolutions.size());
    EXPECT_EQ(*first, *second);
    expectAtlasTilesValid(*first, resolutions);
}

TEST(ShadowAtlas, RejectsUnsupportedResolutionAndAllocatesMoreLayersOnOverflow)
{
    static constexpr auto unsupported = std::array {768U};
    EXPECT_EQ(panda::core::vulkan::renderer::allocateShadowAtlas(unsupported).error().code,
              panda::ErrorCode::InvalidArgument);

    static constexpr auto full = std::array {2048U, 2048U, 2048U, 2048U};
    const auto overflow = panda::core::vulkan::renderer::allocateShadowAtlas(full);
    ASSERT_TRUE(overflow);
    ASSERT_EQ(overflow->size(), full.size());
    for (auto index = std::size_t {}; index < overflow->size(); ++index)
    {
        EXPECT_EQ(overflow->at(index).layer, index);
    }
}

TEST(ShadowAtlas, PacksEveryLightInTheMaximumShadowedSmokeWorkload)
{
    auto view = panda::RenderView {};
    view.camera.position = {0.0F, 0.0F, 5.0F};
    view.lights.reserve(65);
    view.lights.emplace_back(panda::DirectionalLight {.shadow = panda::ShadowSettings {}});
    for (auto index = std::uint32_t {}; index < 64; ++index)
    {
        view.lights.emplace_back(
            panda::PointLight {.range = 8.0F, .shadow = panda::ShadowSettings {.resolution = 256U}});
    }

    const auto plan = panda::core::vulkan::renderer::buildShadowAtlasPlan(view);

    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->views.size(), 385U);
    EXPECT_GT(plan->atlasLayerCount, 1U);
    EXPECT_EQ(plan->viewCountByLight.front(), 1U);
    for (auto index = std::size_t {1}; index < plan->viewCountByLight.size(); ++index)
    {
        EXPECT_EQ(plan->viewCountByLight.at(index), 6U);
    }
    for (const auto& shadowView : plan->views)
    {
        EXPECT_LT(shadowView.tile.layer, plan->atlasLayerCount);
    }
}

TEST(ShadowAtlas, BuildsOnlyRequestedLightViewsWithPointFacesInOrder)
{
    auto view = panda::RenderView {};
    view.lights = {
        panda::SpotLight {.position = {0.0F, 1.0F, 2.0F},  .range = 8.0F,  .shadow = {}                      },
        panda::PointLight {.position = {2.0F, 1.0F, -1.0F}, .range = 12.0F, .shadow = panda::ShadowSettings {}},
        panda::SpotLight {.position = {0.0F, 0.0F, 0.0F},  .range = 6.0F,  .shadow = panda::ShadowSettings {}}
    };

    const auto plan = panda::core::vulkan::renderer::buildShadowAtlasPlan(view);

    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->views.size(), 7U);
    EXPECT_EQ(plan->firstViewByLight, (std::vector {std::numeric_limits<std::uint32_t>::max(), 0U, 6U}));
    EXPECT_EQ(plan->viewCountByLight, (std::vector {0U, 6U, 1U}));
    for (auto face = std::size_t {}; face < 6U; ++face)
    {
        EXPECT_EQ(plan->views.at(face).faceIndex, face);
        EXPECT_EQ(plan->views.at(face).tile.resolution, 512U);
    }
    EXPECT_EQ(plan->views.back().tile.resolution, 1024U);
}

TEST(ShadowAtlas, BuildsAViewForAFullHemisphereSpotLight)
{
    auto view = panda::RenderView {};
    view.lights.emplace_back(panda::SpotLight {
        .position = {0.0F, 1.0F, 2.0F},
        .range = 8.0F,
        .innerConeAngleRadians = 0.0F,
        .outerConeAngleRadians = std::numbers::pi_v<float> / 2.0F,
        .shadow = panda::ShadowSettings {}
    });

    const auto plan = panda::core::vulkan::renderer::buildShadowAtlasPlan(view);

    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->views.size(), 1U);
    const auto projected = plan->views.front().viewProjection * glm::vec4 {1.0F};
    EXPECT_TRUE(std::isfinite(projected.x));
    EXPECT_TRUE(std::isfinite(projected.y));
    EXPECT_TRUE(std::isfinite(projected.z));
    EXPECT_TRUE(std::isfinite(projected.w));
}

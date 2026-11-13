#include "renderer/ShadowAtlas.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/ext/vector_float4.hpp>
#include <limits>
#include <numbers>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <vector>

TEST(ShadowAtlas, AllocatesRequestedTilesDeterministicallyWithGuards)
{
    static constexpr auto resolutions = std::array {2048U, 1024U, 512U, 512U, 512U, 512U, 512U, 512U};

    const auto first = panda::core::vulkan::renderer::allocateShadowAtlas(resolutions);
    const auto second = panda::core::vulkan::renderer::allocateShadowAtlas(resolutions);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(first->size(), resolutions.size());
    EXPECT_EQ(*first, *second);
    for (auto index = std::size_t {}; index < first->size(); ++index)
    {
        const auto& tile = first->at(index);
        EXPECT_EQ(tile.resolution, resolutions.at(index));
        EXPECT_GE(tile.x, panda::core::vulkan::renderer::shadowAtlasGuard);
        EXPECT_GE(tile.y, panda::core::vulkan::renderer::shadowAtlasGuard);
        EXPECT_LE(tile.x + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard,
                  panda::core::vulkan::renderer::shadowAtlasExtent);
        EXPECT_LE(tile.y + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard,
                  panda::core::vulkan::renderer::shadowAtlasExtent);
        for (auto other = index + 1; other < first->size(); ++other)
        {
            const auto& next = first->at(other);
            const auto separated = tile.x + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
                                       next.x - panda::core::vulkan::renderer::shadowAtlasGuard ||
                                   next.x + next.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
                                       tile.x - panda::core::vulkan::renderer::shadowAtlasGuard ||
                                   tile.y + tile.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
                                       next.y - panda::core::vulkan::renderer::shadowAtlasGuard ||
                                   next.y + next.resolution + panda::core::vulkan::renderer::shadowAtlasGuard <=
                                       tile.y - panda::core::vulkan::renderer::shadowAtlasGuard;
            EXPECT_TRUE(separated);
        }
    }
}

TEST(ShadowAtlas, RejectsUnsupportedResolutionAndAtlasOverflow)
{
    static constexpr auto unsupported = std::array {768U};
    EXPECT_EQ(panda::core::vulkan::renderer::allocateShadowAtlas(unsupported).error().code,
              panda::ErrorCode::InvalidArgument);

    static constexpr auto full = std::array {2048U, 2048U, 2048U, 2048U};
    const auto overflow = panda::core::vulkan::renderer::allocateShadowAtlas(full);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, panda::ErrorCode::ResourceExhausted);
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

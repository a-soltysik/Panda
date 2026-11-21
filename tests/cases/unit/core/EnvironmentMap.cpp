#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/gtc/packing.hpp>
#include <limits>
#include <panda/base/Error.hpp>
#include <span>
#include <vector>

#include "EnvironmentMapFilter.hpp"

namespace
{
using Face = std::vector<std::uint16_t>;
using Faces = std::array<Face, 6>;
using FaceViews = std::array<std::span<const std::uint16_t>, 6>;

auto makeFaces(std::uint32_t faceSize, std::array<std::uint16_t, 4> color) -> Faces
{
    auto faces = Faces {};
    for (auto& face : faces)
    {
        face.resize(static_cast<std::size_t>(faceSize) * faceSize * color.size());
        for (auto offset = std::size_t {}; offset < face.size(); offset += color.size())
        {
            std::ranges::copy(color, face.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
    return faces;
}

auto makeViews(const Faces& faces) -> FaceViews
{
    auto views = FaceViews {};
    for (auto index = std::size_t {}; index < views.size(); ++index)
    {
        views.at(index) = faces.at(index);
    }
    return views;
}

void expectConstantMipRadiance(const panda::detail::EnvironmentMapMip& mip, const std::array<std::uint16_t, 4>& color)
{
    for (const auto& face : mip.faces)
    {
        for (auto offset = std::size_t {}; offset < face.size(); offset += color.size())
        {
            EXPECT_EQ(face.at(offset), color.at(0));
            EXPECT_EQ(face.at(offset + 1U), color.at(1));
            EXPECT_EQ(face.at(offset + 2U), color.at(2));
            EXPECT_EQ(face.at(offset + 3U), color.at(3));
        }
    }
}
}

TEST(EnvironmentMapFilter, PreservesMipZeroAndConstantRadianceAcrossTheFullChain)
{
    static constexpr auto faceSize = std::uint32_t {4};
    static constexpr auto color = std::array<std::uint16_t, 4> {0x4000, 0x3c00, 0x3800, 0x3c00};
    const auto faces = makeFaces(faceSize, color);
    const auto result =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = makeViews(faces)}, 32);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->levels.size(), 3U);
    EXPECT_EQ(result->levels.at(0).extent, (glm::uvec2 {faceSize, faceSize}));
    EXPECT_EQ(result->levels.at(0).faces, faces);
    EXPECT_EQ(result->levels.at(1).extent, (glm::uvec2 {2, 2}));
    EXPECT_EQ(result->levels.at(2).extent, (glm::uvec2 {1, 1}));
    for (auto mip = std::size_t {1}; mip < result->levels.size(); ++mip)
    {
        expectConstantMipRadiance(result->levels.at(mip), color);
    }
}

TEST(EnvironmentMapFilter, SpreadsBrightRadianceIntoTheRoughestMip)
{
    static constexpr auto faceSize = std::uint32_t {8};
    static constexpr auto background = std::array<std::uint16_t, 4> {0x2e66, 0x2e66, 0x2e66, 0x3c00};
    auto faces = makeFaces(faceSize, background);
    for (auto row = 3U; row < 5U; ++row)
    {
        for (auto column = 3U; column < 5U; ++column)
        {
            const auto offset = ((static_cast<std::size_t>(row) * faceSize) + column) * background.size();
            faces.at(4).at(offset) = 0x4800;
            faces.at(4).at(offset + 1U) = 0x4600;
            faces.at(4).at(offset + 2U) = 0x4400;
        }
    }
    for (auto row = 3U; row < 5U; ++row)
    {
        const auto offset = ((static_cast<std::size_t>(row) * faceSize) + 7U) * background.size();
        faces.at(4).at(offset) = 0x4800;
    }
    const auto source = panda::detail::EnvironmentMapSource {.faceSize = faceSize, .faces = makeViews(faces)};
    const auto result = panda::detail::prefilterSpecularEnvironmentMap(source, 256);
    const auto singleSampleResult = panda::detail::prefilterSpecularEnvironmentMap(source, 1);

    ASSERT_TRUE(result);
    ASSERT_TRUE(singleSampleResult);
    ASSERT_EQ(result->levels.size(), 4U);
    const auto baseCenterOffset = ((static_cast<std::size_t>(3U) * faceSize) + 3U) * background.size();
    const auto roughCenter = glm::unpackHalf1x16(result->levels.back().faces.at(4).at(0));
    const auto adjacentFace = glm::unpackHalf1x16(result->levels.back().faces.at(0).at(0));
    const auto singleSampleAdjacentFace = glm::unpackHalf1x16(singleSampleResult->levels.back().faces.at(0).at(0));
    EXPECT_FLOAT_EQ(glm::unpackHalf1x16(result->levels.front().faces.at(4).at(baseCenterOffset)), 8.0F);
    EXPECT_GT(roughCenter, glm::unpackHalf1x16(background.at(0)));
    EXPECT_LT(roughCenter, 8.0F);
    EXPECT_GT(adjacentFace, glm::unpackHalf1x16(background.at(0)));
    EXPECT_NE(adjacentFace, singleSampleAdjacentFace);
}

TEST(EnvironmentMapFilter, PrefilterSamplesRadianceAcrossCubemapFaceEdges)
{
    static constexpr auto faceSize = std::uint32_t {2};
    static constexpr auto background = std::array<std::uint16_t, 4> {0, 0, 0, 0x3c00};
    auto topEdgeBright = makeFaces(faceSize, background);
    auto bottomEdgeBright = makeFaces(faceSize, background);
    for (auto column = std::uint32_t {}; column < faceSize; ++column)
    {
        const auto topOffset = (static_cast<std::size_t>(column) * background.size());
        const auto bottomOffset = ((static_cast<std::size_t>(faceSize) + column) * background.size());
        topEdgeBright.at(0).at(topOffset) = 0x4800;
        bottomEdgeBright.at(0).at(bottomOffset) = 0x4800;
    }
    const auto topResult =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = makeViews(topEdgeBright)}, 4);
    const auto bottomResult =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = makeViews(bottomEdgeBright)}, 4);

    ASSERT_TRUE(topResult);
    ASSERT_TRUE(bottomResult);
    const auto topEdge = glm::unpackHalf1x16(topResult->levels.at(1).faces.at(0).at(0));
    const auto bottomEdge = glm::unpackHalf1x16(bottomResult->levels.at(1).faces.at(0).at(0));
    EXPECT_GT(bottomEdge, topEdge);
}

TEST(EnvironmentMapFilter, PreservesSingleLevelMapsAndRejectsInvalidFaceSizes)
{
    static constexpr auto color = std::array<std::uint16_t, 4> {0x4000, 0x3c00, 0x3800, 0x3c00};
    const auto faces = makeFaces(1, color);
    const auto singleLevel =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = 1, .faces = makeViews(faces)}, 16);
    const auto zeroSize = panda::detail::prefilterSpecularEnvironmentMap({.faceSize = 0, .faces = {}}, 16);
    const auto overflowSource = panda::detail::prefilterSpecularEnvironmentMap(
        {.faceSize = std::numeric_limits<std::uint32_t>::max(), .faces = {}},
        16);
    const auto overflowSize = panda::detail::validateEnvironmentFace({}, std::numeric_limits<std::uint32_t>::max());

    ASSERT_TRUE(singleLevel);
    ASSERT_EQ(singleLevel->levels.size(), 1U);
    EXPECT_EQ(singleLevel->levels.front().faces, faces);
    ASSERT_FALSE(zeroSize);
    EXPECT_EQ(zeroSize.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(overflowSource);
    EXPECT_EQ(overflowSource.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_FALSE(overflowSize);
    EXPECT_EQ(overflowSize.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST(EnvironmentMapFilter, RejectsInvalidInputWithoutChangingSourceFaces)
{
    static constexpr auto faceSize = std::uint32_t {2};
    const auto color = std::array<std::uint16_t, 4> {0x3c00, 0x3c00, 0x3c00, 0x3c00};
    auto faces = makeFaces(faceSize, color);
    const auto originalFaces = faces;
    const auto views = makeViews(faces);

    const auto zeroSamples = panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = views}, 0);
    auto shortViews = views;
    shortViews.at(0) = shortViews.at(0).first(shortViews.at(0).size() - 1U);
    const auto shortFace =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = shortViews}, 128);
    faces.at(0).at(0) = 0xbc00;
    const auto invalidFaces = faces;
    const auto negativeRadiance =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = makeViews(faces)}, 128);
    auto nonfiniteFaces = makeFaces(faceSize, color);
    nonfiniteFaces.at(0).at(0) = 0x7c00;
    const auto nonfiniteRadiance =
        panda::detail::prefilterSpecularEnvironmentMap({.faceSize = faceSize, .faces = makeViews(nonfiniteFaces)}, 128);

    EXPECT_FALSE(zeroSamples);
    EXPECT_EQ(zeroSamples.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(shortFace);
    EXPECT_EQ(shortFace.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(negativeRadiance);
    EXPECT_EQ(negativeRadiance.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(nonfiniteRadiance);
    EXPECT_EQ(nonfiniteRadiance.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(originalFaces.at(0).at(0), color.at(0));
    EXPECT_EQ(faces, invalidFaces);
}

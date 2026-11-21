#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/gtc/packing.hpp>
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

    EXPECT_FALSE(zeroSamples);
    EXPECT_EQ(zeroSamples.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(shortFace);
    EXPECT_EQ(shortFace.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(negativeRadiance);
    EXPECT_EQ(negativeRadiance.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(originalFaces.at(0).at(0), color.at(0));
    EXPECT_EQ(faces, invalidFaces);
}

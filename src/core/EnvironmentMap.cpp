#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <limits>
#include <panda/base/Error.hpp>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "EnvironmentMapFilter.hpp"

namespace panda::detail
{
namespace
{
struct CubeCoordinates
{
    std::uint32_t face {0};
    glm::vec2 uv {};
};

struct HammersleyPoint
{
    float x {0.0F};
    float y {0.0F};
};

struct TangentBasis
{
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

struct FilterContext
{
    const EnvironmentMapSource& source;
    float alphaSquared {0.0F};
    std::span<const HammersleyPoint> samples;
};

constexpr auto rgbaChannelCount = std::size_t {4};
constexpr auto rgbChannelCount = std::size_t {3};

auto radicalInverse(std::uint32_t bits) -> float
{
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

auto makeHammersleyPoints(std::uint32_t count) -> std::vector<HammersleyPoint>
{
    auto points = std::vector<HammersleyPoint>(count);
    for (auto index = std::uint32_t {}; index < count; ++index)
    {
        points.at(index) = {.x = static_cast<float>(index) / static_cast<float>(count), .y = radicalInverse(index)};
    }
    return points;
}

auto directionForFace(std::uint32_t face, glm::vec2 uv) -> glm::vec3
{
    const auto faceS = (2.0F * uv.x) - 1.0F;
    const auto faceT = (2.0F * uv.y) - 1.0F;
    switch (face)
    {
    case 0:
        return {1.0F, -faceT, -faceS};
    case 1:
        return {-1.0F, -faceT, faceS};
    case 2:
        return {faceS, 1.0F, faceT};
    case 3:
        return {faceS, -1.0F, -faceT};
    case 4:
        return {faceS, -faceT, 1.0F};
    default:
        return {-faceS, -faceT, -1.0F};
    }
}

auto makeCubeCoordinates(std::uint32_t face, float faceS, float faceT, float majorAxis) -> CubeCoordinates
{
    return {
        .face = face,
        .uv = {(((faceS / majorAxis) + 1.0F) * 0.5F), (((faceT / majorAxis) + 1.0F) * 0.5F)}
    };
}

auto coordinatesForX(glm::vec3 direction, float majorAxis) -> CubeCoordinates
{
    if (direction.x > 0.0F)
    {
        return makeCubeCoordinates(0, -direction.z, -direction.y, majorAxis);
    }
    return makeCubeCoordinates(1, direction.z, -direction.y, majorAxis);
}

auto coordinatesForY(glm::vec3 direction, float majorAxis) -> CubeCoordinates
{
    if (direction.y > 0.0F)
    {
        return makeCubeCoordinates(2, direction.x, direction.z, majorAxis);
    }
    return makeCubeCoordinates(3, direction.x, -direction.z, majorAxis);
}

auto coordinatesForZ(glm::vec3 direction, float majorAxis) -> CubeCoordinates
{
    if (direction.z > 0.0F)
    {
        return makeCubeCoordinates(4, direction.x, -direction.y, majorAxis);
    }
    return makeCubeCoordinates(5, -direction.x, -direction.y, majorAxis);
}

auto coordinatesForDirection(glm::vec3 direction) -> CubeCoordinates
{
    const auto absolute = glm::abs(direction);
    if (absolute.x > absolute.y && absolute.x > absolute.z)
    {
        return coordinatesForX(direction, absolute.x);
    }
    if (absolute.y > absolute.z)
    {
        return coordinatesForY(direction, absolute.y);
    }
    return coordinatesForZ(direction, absolute.z);
}

auto readRgba16f(std::span<const std::uint16_t> pixels, std::size_t offset) -> std::array<std::uint16_t, 4>
{
    auto rgba = std::array<std::uint16_t, rgbaChannelCount> {};
    std::ranges::copy(pixels.subspan(offset, rgba.size()), rgba.begin());
    return rgba;
}

auto readTexel(const EnvironmentMapSource& source, std::uint32_t face, std::int64_t x, std::int64_t y) -> glm::vec3
{
    const auto size = static_cast<std::int64_t>(source.faceSize);
    if (x < 0 || y < 0 || x >= size || y >= size)
    {
        const auto uv = glm::vec2 {(static_cast<float>(x) + 0.5F) / static_cast<float>(source.faceSize),
                                   (static_cast<float>(y) + 0.5F) / static_cast<float>(source.faceSize)};
        const auto adjacent = coordinatesForDirection(directionForFace(face, uv));
        face = adjacent.face;
        x = std::clamp(static_cast<std::int64_t>(adjacent.uv.x * static_cast<float>(source.faceSize)),
                       std::int64_t {},
                       size - 1);
        y = std::clamp(static_cast<std::int64_t>(adjacent.uv.y * static_cast<float>(source.faceSize)),
                       std::int64_t {},
                       size - 1);
    }
    const auto offset =
        ((static_cast<std::size_t>(y) * source.faceSize) + static_cast<std::size_t>(x)) * rgbaChannelCount;
    const auto& pixels = source.faces.at(face);
    const auto rgba = readRgba16f(pixels, offset);
    return {glm::unpackHalf1x16(rgba.at(0)), glm::unpackHalf1x16(rgba.at(1)), glm::unpackHalf1x16(rgba.at(2))};
}

auto sampleCube(const EnvironmentMapSource& source, glm::vec3 direction) -> glm::vec3
{
    const auto coordinates = coordinatesForDirection(direction);
    const auto position = (coordinates.uv * static_cast<float>(source.faceSize)) - glm::vec2 {0.5F};
    const auto x0 = static_cast<std::int64_t>(std::floor(position.x));
    const auto y0 = static_cast<std::int64_t>(std::floor(position.y));
    const auto fraction = glm::vec2 {position.x - static_cast<float>(x0), position.y - static_cast<float>(y0)};
    const auto top = glm::mix(readTexel(source, coordinates.face, x0, y0),
                              readTexel(source, coordinates.face, x0 + 1, y0),
                              fraction.x);
    const auto bottom = glm::mix(readTexel(source, coordinates.face, x0, y0 + 1),
                                 readTexel(source, coordinates.face, x0 + 1, y0 + 1),
                                 fraction.x);
    return glm::mix(top, bottom, fraction.y);
}

auto makeTangentBasis(std::uint32_t face, glm::vec2 uv) -> TangentBasis
{
    const auto normal = glm::normalize(directionForFace(face, uv));
    const auto up = std::abs(normal.z) < 0.999F ? glm::vec3 {0.0F, 0.0F, 1.0F} : glm::vec3 {1.0F, 0.0F, 0.0F};
    const auto tangent = glm::normalize(glm::cross(up, normal));
    return {.normal = normal, .tangent = tangent, .bitangent = glm::cross(normal, tangent)};
}

auto importanceSampleDirection(const TangentBasis& basis, HammersleyPoint sample, float alphaSquared) -> glm::vec3
{
    const auto phi = 2.0F * glm::pi<float>() * sample.x;
    const auto cosTheta = std::sqrt((1.0F - sample.y) / (1.0F + ((alphaSquared - 1.0F) * sample.y)));
    const auto sinTheta = std::sqrt(std::max(0.0F, 1.0F - (cosTheta * cosTheta)));
    const auto halfVector = glm::normalize((basis.tangent * (sinTheta * std::cos(phi))) +
                                           (basis.bitangent * (sinTheta * std::sin(phi))) + (basis.normal * cosTheta));
    return glm::normalize((2.0F * glm::dot(basis.normal, halfVector) * halfVector) - basis.normal);
}

auto prefilterTexel(const FilterContext& context, const TangentBasis& basis) -> glm::vec3
{
    auto radiance = glm::vec3 {};
    auto totalWeight = 0.0F;
    for (const auto sample : context.samples)
    {
        const auto light = importanceSampleDirection(basis, sample, context.alphaSquared);
        const auto normalDotLight = std::max(glm::dot(basis.normal, light), 0.0F);
        if (normalDotLight > 0.0F)
        {
            // shortcut: sample only mip zero; add source LOD if high-frequency maps need fewer samples.
            radiance += sampleCube(context.source, light) * normalDotLight;
            totalWeight += normalDotLight;
        }
    }
    return totalWeight > 0.0F ? radiance / totalWeight : sampleCube(context.source, basis.normal);
}

auto hasValidRadiance(const std::array<std::uint16_t, rgbaChannelCount>& rgba) -> bool
{
    for (auto channel = std::size_t {}; channel < rgbChannelCount; ++channel)
    {
        const auto value = glm::unpackHalf1x16(rgba.at(channel));
        if (!std::isfinite(value) || value < 0.0F)
        {
            return false;
        }
    }
    return true;
}

auto validateFace(std::span<const std::uint16_t> face, std::size_t valueCount) -> Result<void>
{
    if (face.size() != valueCount)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Environment cubemap face does not match its declared size")};
    }
    for (auto offset = std::size_t {}; offset < face.size(); offset += rgbaChannelCount)
    {
        const auto rgba = readRgba16f(face, offset);
        if (!hasValidRadiance(rgba))
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Environment radiance must be finite and nonnegative")};
        }
    }
    return {};
}

auto validateSource(const EnvironmentMapSource& source) -> Result<void>
{
    if (source.faceSize == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Environment cubemap face size must be nonzero")};
    }
    const auto size = static_cast<std::size_t>(source.faceSize);
    if (size > std::numeric_limits<std::size_t>::max() / size / rgbaChannelCount)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Environment cubemap face is too large")};
    }
    const auto valueCount = size * size * rgbaChannelCount;
    for (const auto face : source.faces)
    {
        if (const auto valid = validateFace(face, valueCount); !valid)
        {
            return valid;
        }
    }
    return {};
}

auto mipLevelCount(std::uint32_t size) -> std::uint32_t
{
    auto count = std::uint32_t {1};
    while (size > 1U)
    {
        size = std::max(1U, size / 2U);
        ++count;
    }
    return count;
}

void writeFilteredFace(EnvironmentMapMip& mip, std::uint32_t face, const FilterContext& context)
{
    static constexpr auto maximumHalfFloat = 65504.0F;
    auto& output = mip.faces.at(face);
    output.resize(static_cast<std::size_t>(mip.extent.x) * mip.extent.y * rgbaChannelCount);
    for (auto row = std::uint32_t {}; row < mip.extent.y; ++row)
    {
        for (auto column = std::uint32_t {}; column < mip.extent.x; ++column)
        {
            const auto uv = glm::vec2 {(static_cast<float>(column) + 0.5F) / static_cast<float>(mip.extent.x),
                                       (static_cast<float>(row) + 0.5F) / static_cast<float>(mip.extent.y)};
            const auto basis = makeTangentBasis(face, uv);
            const auto color = prefilterTexel(context, basis);
            const auto offset = ((static_cast<std::size_t>(row) * mip.extent.x) + column) * rgbaChannelCount;
            output.at(offset) = glm::packHalf1x16(std::clamp(color.r, 0.0F, maximumHalfFloat));
            output.at(offset + 1U) = glm::packHalf1x16(std::clamp(color.g, 0.0F, maximumHalfFloat));
            output.at(offset + 2U) = glm::packHalf1x16(std::clamp(color.b, 0.0F, maximumHalfFloat));
            output.at(offset + 3U) = glm::packHalf1x16(1.0F);
        }
    }
}

void writeFilteredMip(EnvironmentMapMip& mip, const FilterContext& context)
{
    for (auto face = std::uint32_t {}; face < mip.faces.size(); ++face)
    {
        writeFilteredFace(mip, face, context);
    }
}

auto makeSourceMip(const EnvironmentMapSource& source) -> EnvironmentMapMip
{
    auto mip = EnvironmentMapMip {
        .extent = {source.faceSize, source.faceSize},
        .faces = {}
    };
    for (auto face = std::size_t {}; face < mip.faces.size(); ++face)
    {
        mip.faces.at(face).assign(source.faces.at(face).begin(), source.faces.at(face).end());
    }
    return mip;
}

auto buildMipChain(const EnvironmentMapSource& source, std::uint32_t samplesPerTexel) -> SpecularEnvironmentMipChain
{
    const auto levelCount = mipLevelCount(source.faceSize);
    auto chain = SpecularEnvironmentMipChain {};
    chain.levels.reserve(levelCount);
    chain.levels.push_back(makeSourceMip(source));
    if (levelCount == 1)
    {
        return chain;
    }

    const auto samples = makeHammersleyPoints(samplesPerTexel);
    for (auto mipLevel = std::uint32_t {1}; mipLevel < levelCount; ++mipLevel)
    {
        const auto mipSize = std::max(1U, source.faceSize >> mipLevel);
        auto mip = EnvironmentMapMip {
            .extent = {mipSize, mipSize},
            .faces = {}
        };
        const auto roughness = static_cast<float>(mipLevel) / static_cast<float>(levelCount - 1U);
        const auto alpha = roughness * roughness;
        const auto context = FilterContext {.source = source, .alphaSquared = alpha * alpha, .samples = samples};
        writeFilteredMip(mip, context);
        chain.levels.push_back(std::move(mip));
    }
    return chain;
}
}

auto validateEnvironmentFace(std::span<const std::uint16_t> face, std::uint32_t faceSize) -> Result<void>
{
    if (faceSize == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Environment cubemap face size must be nonzero")};
    }
    const auto size = static_cast<std::size_t>(faceSize);
    if (size > std::numeric_limits<std::size_t>::max() / size / rgbaChannelCount)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Environment cubemap face is too large")};
    }
    return validateFace(face, size * size * rgbaChannelCount);
}

auto prefilterSpecularEnvironmentMap(const EnvironmentMapSource& source, std::uint32_t samplesPerTexel)
    -> Result<SpecularEnvironmentMipChain>
{
    if (samplesPerTexel == 0)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Environment filter sample count must be nonzero")};
    }
    if (const auto valid = validateSource(source); !valid)
    {
        return std::unexpected {valid.error()};
    }
    try
    {
        return buildMipChain(source, samplesPerTexel);
    }
    catch (const std::length_error&)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Environment mip chain exceeds container limits")};
    }
}
}

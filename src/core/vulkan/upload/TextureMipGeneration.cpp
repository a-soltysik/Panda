#include "TextureMipGeneration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <panda/base/Error.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <span>
#include <utility>
#include <vector>

namespace panda::core::vulkan::upload
{
namespace
{
struct MipExtent
{
    std::uint32_t width {0};
    std::uint32_t height {0};
};

struct MipImageView
{
    std::span<const std::byte> pixels;
    MipExtent extent {};
};

struct MipPixel
{
    std::uint32_t x {0};
    std::uint32_t y {0};
};

struct RowPitch
{
    std::uint64_t source {0};
    std::uint64_t tight {0};
};

struct MipSourceLayout
{
    MipExtent extent;
    RowPitch rowPitch;
    std::size_t byteSize;
    bool srgb;
};

struct SourceRange
{
    std::uint32_t begin {0};
    std::uint32_t end {0};
};

auto srgbToLinear(std::uint8_t value) -> float
{
    const auto encoded = static_cast<float>(value) / 255.0F;
    return encoded <= 0.04045F ? encoded / 12.92F : std::pow((encoded + 0.055F) / 1.055F, 2.4F);
}

auto linearToSrgbByte(float value) -> std::byte
{
    const auto linear = std::clamp(value, 0.0F, 1.0F);
    const auto encoded = linear <= 0.0031308F ? linear * 12.92F : (1.055F * std::pow(linear, 1.0F / 2.4F)) - 0.055F;
    return static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(encoded * 255.0F)));
}

auto byteValue(std::byte value) -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(value);
}

auto isSrgb(PixelFormat format) -> bool
{
    return format == PixelFormat::Rgba8Srgb || format == PixelFormat::Bgra8Srgb;
}

auto isSupported(PixelFormat format) -> bool
{
    return isSrgb(format) || format == PixelFormat::Rgba8Unorm || format == PixelFormat::Bgra8Unorm;
}

auto checkedByteSize(MipExtent extent) -> Result<std::size_t>
{
    constexpr auto bytesPerTexel = std::size_t {4};
    const auto width = static_cast<std::size_t>(extent.width);
    const auto height = static_cast<std::size_t>(extent.height);
    if (width == 0 || height == 0 || width > std::numeric_limits<std::size_t>::max() / bytesPerTexel / height)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Texture mip level is too large")};
    }
    return width * height * bytesPerTexel;
}

auto sourceRange(std::uint32_t destinationIndex, std::uint32_t sourceExtent, std::uint32_t destinationExtent)
    -> SourceRange
{
    const auto begin = (static_cast<std::uint64_t>(destinationIndex) * sourceExtent) / destinationExtent;
    const auto end = (static_cast<std::uint64_t>(destinationIndex + 1U) * sourceExtent) / destinationExtent;
    return {.begin = static_cast<std::uint32_t>(begin), .end = static_cast<std::uint32_t>(end)};
}

auto validateBaseLevel(const TextureMipSource& source, std::uint64_t tightRowPitch) -> Result<RowPitch>
{
    const auto rowPitch = source.rowPitchBytes == 0 ? tightRowPitch : source.rowPitchBytes;
    if (rowPitch < tightRowPitch || rowPitch % 4U != 0 ||
        (source.height > 1 &&
         rowPitch > (std::numeric_limits<std::uint64_t>::max() - tightRowPitch) / (source.height - 1U)))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture mip source row pitch is invalid")};
    }
    const auto requiredBytes = (static_cast<std::uint64_t>(source.height - 1U) * rowPitch) + tightRowPitch;
    if (requiredBytes > source.baseLevel.size())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture mip source data is too small")};
    }
    return RowPitch {.source = rowPitch, .tight = tightRowPitch};
}

void copyBaseLevel(const TextureMipSource& source, std::span<std::byte> destination, RowPitch rowPitch)
{
    for (auto rowIndex = std::uint32_t {}; rowIndex < source.height; ++rowIndex)
    {
        const auto sourceOffset = static_cast<std::uint64_t>(rowIndex) * rowPitch.source;
        const auto destinationOffset = static_cast<std::uint64_t>(rowIndex) * rowPitch.tight;
        const auto rowSize = rowPitch.tight;
        const auto sourceRow = source.baseLevel.subspan(sourceOffset, rowSize);
        auto destinationRow = destination.subspan(destinationOffset, rowSize);
        std::ranges::copy(sourceRow, destinationRow.begin());
    }
}

auto decodeChannel(std::byte value, bool srgb, std::size_t channel) -> float
{
    const auto byte = byteValue(value);
    return srgb && channel < 3 ? srgbToLinear(byte) : static_cast<float>(byte) / 255.0F;
}

auto encodeChannel(float value, bool srgb, std::size_t channel) -> std::byte
{
    if (srgb && channel < 3)
    {
        return linearToSrgbByte(value);
    }
    return static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(value * 255.0F)));
}

void accumulateTexel(std::array<float, 4>& sums, const MipImageView& source, MipPixel pixel, bool srgb)
{
    const auto offset = ((static_cast<std::size_t>(pixel.y) * source.extent.width) + pixel.x) * 4U;
    for (auto channel = std::size_t {}; channel < sums.size(); ++channel)
    {
        sums.at(channel) += decodeChannel(source.pixels[offset + channel], srgb, channel);
    }
}

auto encodeAverage(const std::array<float, 4>& sums, float sampleCount, bool srgb) -> std::array<std::byte, 4>
{
    auto result = std::array<std::byte, 4> {};
    for (auto channel = std::size_t {}; channel < sums.size(); ++channel)
    {
        result.at(channel) = encodeChannel(sums.at(channel) / sampleCount, srgb, channel);
    }
    return result;
}

auto averageTexel(const MipImageView& source, MipExtent destinationExtent, MipPixel destinationPixel, bool srgb)
    -> std::array<std::byte, 4>
{
    const auto rows = sourceRange(destinationPixel.y, source.extent.height, destinationExtent.height);
    const auto columns = sourceRange(destinationPixel.x, source.extent.width, destinationExtent.width);
    auto sums = std::array<float, 4> {};
    const auto sampleCount = static_cast<float>((columns.end - columns.begin) * (rows.end - rows.begin));
    for (auto sourceY = rows.begin; sourceY < rows.end; ++sourceY)
    {
        for (auto sourceX = columns.begin; sourceX < columns.end; ++sourceX)
        {
            accumulateTexel(sums, source, MipPixel {.x = sourceX, .y = sourceY}, srgb);
        }
    }
    return encodeAverage(sums, sampleCount, srgb);
}

auto generateNextMip(const MipImageView& source, MipExtent destinationExtent, bool srgb)
    -> Result<std::vector<std::byte>>
{
    const auto byteSize = checkedByteSize(destinationExtent);
    if (!byteSize)
    {
        return std::unexpected {byteSize.error()};
    }
    auto destination = std::vector<std::byte>(*byteSize);
    for (auto rowIndex = std::uint32_t {}; rowIndex < destinationExtent.height; ++rowIndex)
    {
        for (auto columnIndex = std::uint32_t {}; columnIndex < destinationExtent.width; ++columnIndex)
        {
            const auto pixel =
                averageTexel(source, destinationExtent, MipPixel {.x = columnIndex, .y = rowIndex}, srgb);
            const auto texelOffset =
                ((static_cast<std::size_t>(rowIndex) * destinationExtent.width) + columnIndex) * 4U;
            auto destinationTexel = std::span {destination}.subspan(texelOffset, pixel.size());
            std::ranges::copy(pixel, destinationTexel.begin());
        }
    }
    return destination;
}

auto validateMipSource(const TextureMipSource& source) -> Result<MipSourceLayout>
{
    if (!isSupported(source.format))
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Mip generation supports only RGBA8 textures")};
    }
    const auto extent = MipExtent {.width = source.width, .height = source.height};
    const auto byteSize = checkedByteSize(extent);
    if (!byteSize)
    {
        return std::unexpected {byteSize.error()};
    }
    const auto tightRowPitch = static_cast<std::uint64_t>(source.width) * 4U;
    const auto rowPitch = validateBaseLevel(source, tightRowPitch);
    if (!rowPitch)
    {
        return std::unexpected {rowPitch.error()};
    }
    return MipSourceLayout {.extent = extent,
                            .rowPitch = *rowPitch,
                            .byteSize = *byteSize,
                            .srgb = isSrgb(source.format)};
}

auto appendMipLevels(TextureMipChain& chain, MipExtent sourceExtent, bool srgb) -> Result<void>
{
    while (sourceExtent.width > 1 || sourceExtent.height > 1)
    {
        const auto destinationExtent = MipExtent {.width = std::max(1U, sourceExtent.width / 2U),
                                                  .height = std::max(1U, sourceExtent.height / 2U)};
        auto next = generateNextMip({.pixels = chain.back(), .extent = sourceExtent}, destinationExtent, srgb);
        if (!next)
        {
            return std::unexpected {next.error()};
        }
        chain.push_back(std::move(*next));
        sourceExtent = destinationExtent;
    }
    return {};
}

auto buildMipChain(const TextureMipSource& source, const MipSourceLayout& layout) -> Result<TextureMipChain>
{
    auto chain = TextureMipChain {};
    chain.emplace_back(layout.byteSize);
    copyBaseLevel(source, chain.front(), layout.rowPitch);
    const auto appended = appendMipLevels(chain, layout.extent, layout.srgb);
    if (!appended)
    {
        return std::unexpected {appended.error()};
    }
    return chain;
}
}

auto generateTextureMipChain(const TextureMipSource& source) -> Result<TextureMipChain>
{
    const auto layout = validateMipSource(source);
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    return buildMipChain(source, *layout);
}
}

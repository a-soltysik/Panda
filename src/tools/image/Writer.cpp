#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <new>
#include <panda/base/Error.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/tools/image/Writer.hpp>
#include <string_view>
#include <vector>

namespace panda::tools::image
{
namespace
{
constexpr auto rgbaChannels = std::size_t {4};
constexpr auto rgbChannels = std::size_t {3};
constexpr auto srgbLinearThreshold = 0.04045F;
constexpr auto srgbLinearScale = 12.92F;
constexpr auto srgbOffset = 0.055F;
constexpr auto srgbScale = 1.055F;
constexpr auto srgbGamma = 2.4F;
constexpr auto byteScale = 255.0F;
constexpr auto maximumJpegQuality = std::uint8_t {100};

struct FileOutput
{
    std::ofstream& stream;
    bool failed {false};
};

void writeChunk(void* userData, void* data, int size) noexcept
{
    auto& output = *static_cast<FileOutput*>(userData);
    if (size < 0 || output.failed)
    {
        output.failed = true;
        return;
    }
    if (size == 0)
    {
        return;
    }
    try
    {
        output.stream.write(static_cast<const char*>(data), size);
        output.failed = !output.stream;
    }
    catch (...)
    {
        output.failed = true;
    }
}

auto validate(const ImageReadback& image) -> Result<void>
{
    const auto width = static_cast<std::size_t>(image.extent.x);
    const auto height = static_cast<std::size_t>(image.extent.y);
    if (width == 0 || height == 0 || width > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        width > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / rgbaChannels ||
        width > std::numeric_limits<std::size_t>::max() / height)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "ImageReadback extent is outside encoder limits")};
    }

    const auto pixels = width * height;
    if (pixels > static_cast<std::size_t>(std::numeric_limits<int>::max()) / rgbaChannels ||
        image.rgba.size() != pixels * rgbaChannels)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "ImageReadback must contain exactly four bytes per pixel")};
    }
    return {};
}

template <typename Encode>
auto writeEncodedFile(const std::filesystem::path& path, std::string_view format, Encode encode) -> Result<void>
{
    // The callback streams encoder chunks through C++ streams and native filesystem paths.
    auto stream = std::ofstream {path, std::ios::binary};
    if (!stream)
    {
        return std::unexpected {
            makeError(ErrorCode::BackendFailure, std::format("Could not open {} output path", format))};
    }

    // shortcut: failed writes may leave a partial file; use temporary-file replacement if atomic saves are required.
    auto output = FileOutput {.stream = stream};
    const auto encoded = encode(writeChunk, &output);
    stream.flush();
    stream.close();
    if (output.failed || stream.fail())
    {
        return std::unexpected {
            makeError(ErrorCode::BackendFailure, std::format("Could not write {} output path", format))};
    }
    if (encoded == 0)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, std::format("Could not encode {} image", format))};
    }
    return {};
}

auto decodeSrgb(std::uint8_t value) -> float
{
    const auto encoded = static_cast<float>(value) / byteScale;
    return encoded <= srgbLinearThreshold ? encoded / srgbLinearScale
                                          : std::pow((encoded + srgbOffset) / srgbScale, srgbGamma);
}

auto encodeSrgb(float value) -> std::uint8_t
{
    const auto encoded =
        value <= 0.0031308F ? value * srgbLinearScale : (srgbScale * std::pow(value, 1.0F / srgbGamma)) - srgbOffset;
    return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0F, 1.0F) * byteScale));
}

auto compositeOverBlack(const ImageReadback& image) -> std::vector<std::uint8_t>
{
    auto pixels = std::vector<std::uint8_t>(image.rgba.size() / rgbaChannels * rgbChannels);
    const auto count = image.rgba.size() / rgbaChannels;
    for (auto pixel = std::size_t {}; pixel < count; ++pixel)
    {
        const auto source = pixel * rgbaChannels;
        const auto destination = pixel * rgbChannels;
        const auto alpha = static_cast<float>(image.rgba.at(source + 3)) / byteScale;
        pixels.at(destination) = encodeSrgb(decodeSrgb(image.rgba.at(source)) * alpha);
        pixels.at(destination + 1) = encodeSrgb(decodeSrgb(image.rgba.at(source + 1)) * alpha);
        pixels.at(destination + 2) = encodeSrgb(decodeSrgb(image.rgba.at(source + 2)) * alpha);
    }
    return pixels;
}
}

auto writeRawRgba(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>
{
    const auto valid = validate(image);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    auto stream = std::ofstream {path, std::ios::binary};
    if (!stream)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Could not open raw RGBA output path")};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): ostream::write accepts bytes only through char storage.
    stream.write(reinterpret_cast<const char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    stream.flush();
    stream.close();
    if (!stream)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Could not write raw RGBA output path")};
    }
    return {};
}

auto writePng(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>
{
    const auto valid = validate(image);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    const auto width = static_cast<int>(image.extent.x);
    const auto height = static_cast<int>(image.extent.y);
    const auto stride = static_cast<std::ptrdiff_t>(image.extent.x) * static_cast<std::ptrdiff_t>(rgbaChannels);
    return writeEncodedFile(path, "PNG", [&](stbi_write_func* callback, void* context) {
        return stbi_write_png_to_func(callback, context, width, height, 4, image.rgba.data(), stride);
    });
}

auto writeBmp(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>
{
    const auto valid = validate(image);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    try
    {
        const auto pixels = compositeOverBlack(image);
        const auto width = static_cast<int>(image.extent.x);
        const auto height = static_cast<int>(image.extent.y);
        const auto stride = static_cast<std::ptrdiff_t>(image.extent.x) * static_cast<std::ptrdiff_t>(rgbChannels);
        return writeEncodedFile(path, "BMP", [&](stbi_write_func* callback, void* context) {
            return stbi_write_bmp_strided_to_func(callback, context, width, height, 3, pixels.data(), stride);
        });
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Could not allocate pixels for BMP export")};
    }
}

auto writeJpeg(const ImageReadback& image, const std::filesystem::path& path, std::uint8_t quality) -> Result<void>
{
    if (quality == 0 || quality > maximumJpegQuality)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "JPEG quality must be between 1 and 100")};
    }
    const auto valid = validate(image);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    try
    {
        const auto pixels = compositeOverBlack(image);
        const auto width = static_cast<int>(image.extent.x);
        const auto height = static_cast<int>(image.extent.y);
        const auto stride = static_cast<std::ptrdiff_t>(image.extent.x) * static_cast<std::ptrdiff_t>(rgbChannels);
        return writeEncodedFile(path, "JPEG", [&](stbi_write_func* callback, void* context) {
            return stbi_write_jpg_strided_to_func(callback, context, width, height, 3, pixels.data(), stride, quality);
        });
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Could not allocate pixels for JPEG export")};
    }
}
}

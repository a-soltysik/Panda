#pragma once

/// @file
/// CPU image file encoding for completed offscreen readbacks.

#include <cstdint>
#include <filesystem>
#include <panda/base/Error.hpp>
#include <panda/resources/OffscreenTarget.hpp>

namespace panda::tools::image
{
inline constexpr auto defaultJpegQuality = std::uint8_t {90};

/// @brief Writes the readback's top-to-bottom RGBA bytes without a header or metadata.
/// @param image Completed RGBA8 readback; its extent must be stored separately if needed to read the file.
/// @param path Destination path; the parent directory must already exist.
/// @return Success or invalid image data or filesystem failure.
[[nodiscard]] auto writeRawRgba(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>;

/// @brief Writes top-to-bottom RGBA pixels to a PNG file without changing their channels.
/// @param image Completed RGBA8 readback; RGB bytes are sRGB encoded and alpha is linear UNORM.
/// @param path Destination path; the parent directory must already exist.
/// @return Success or invalid image data, encoder allocation, or filesystem failure.
[[nodiscard]] auto writePng(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>;

/// @brief Writes an uncompressed 24-bit BMP, compositing alpha over black in linear color.
/// @param image Completed RGBA8 readback; RGB bytes are sRGB encoded and alpha is linear UNORM.
/// @param path Destination path; the parent directory must already exist.
/// @return Success or invalid image data, encoder allocation, or filesystem failure.
[[nodiscard]] auto writeBmp(const ImageReadback& image, const std::filesystem::path& path) -> Result<void>;

/// @brief Writes a JPEG after compositing alpha over black in linear color.
/// @param image Completed RGBA8 readback; RGB bytes are sRGB encoded and alpha is linear UNORM.
/// @param path Destination path; the parent directory must already exist.
/// @param quality Compression quality from 1 (lowest) to 100 (highest), defaulting to 90.
/// @return Success or invalid image data/quality, encoder allocation, or filesystem failure.
[[nodiscard]] auto writeJpeg(const ImageReadback& image,
                             const std::filesystem::path& path,
                             std::uint8_t quality = defaultJpegQuality) -> Result<void>;

}

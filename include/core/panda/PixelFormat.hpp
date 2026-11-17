#pragma once

/// @file
/// Supported public texture formats.

#include <cstdint>

namespace panda
{
/// @brief Color formats supported by public texture upload and material paths.
enum class PixelFormat : std::uint8_t
{
    /// Invalid/default format; texture creation rejects it.
    Undefined = 0,
    /// Four-channel, 8-bit UNORM color.
    Rgba8Unorm,
    /// Four-channel, 8-bit sRGB color.
    Rgba8Srgb,
    /// Four-channel, 8-bit BGRA UNORM color.
    Bgra8Unorm,
    /// Four-channel, 8-bit BGRA sRGB color.
    Bgra8Srgb,
};
}

#pragma once

/// @file
/// User-facing texture filtering quality presets.

#include <cstdint>

namespace panda
{
/// @brief Common texture filtering choices applied to all built-in material maps in an Engine.
enum class TextureFilteringPreset : std::uint8_t
{
    /// Nearest-neighbor texel and mip selection.
    Nearest,
    /// Linear texel filtering with nearest mip selection (bilinear).
    Bilinear,
    /// Linear texel filtering and interpolation between mip levels (trilinear).
    Trilinear,
    /// Trilinear filtering with the requested anisotropic level.
    Anisotropic2x,
    Anisotropic4x,
    Anisotropic8x,
    Anisotropic16x,
};
}

#pragma once

/// @file
/// Sample counts for mesh rendering.

#include <cstdint>

namespace panda
{
/// @brief Requested or effective color and depth sample count.
enum class SampleCount : std::uint8_t
{
    /// One sample per pixel, with MSAA disabled.
    One = 1,
    /// Two samples per pixel.
    Two = 2,
    /// Four samples per pixel.
    Four = 4,
};
}

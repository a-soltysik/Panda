#pragma once

#include <cstddef>
#include <cstdint>
#include <panda/base/Error.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <span>
#include <vector>

namespace panda::core::vulkan::upload
{
using TextureMipChain = std::vector<std::vector<std::byte>>;

struct TextureMipSource
{
    PixelFormat format {PixelFormat::Undefined};
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::span<const std::byte> baseLevel;
    std::uint64_t rowPitchBytes {0};
};

[[nodiscard]] auto generateTextureMipChain(const TextureMipSource& source) -> Result<TextureMipChain>;
}

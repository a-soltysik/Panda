#pragma once

#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/PixelFormat.hpp>
#include <panda/Texture.hpp>

#include "VulkanHpp.hpp"

namespace panda::detail
{
[[nodiscard]] auto mapPixelFormat(PixelFormat format) -> Result<vk::Format>;
[[nodiscard]] auto mapBufferUsage(BufferUsage usage) -> Result<vk::BufferUsageFlags>;
[[nodiscard]] auto mapTextureUsage(TextureUsage usage) -> Result<vk::ImageUsageFlags>;
}

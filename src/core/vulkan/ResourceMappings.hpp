#pragma once

#include <panda/base/Error.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>

#include "VulkanHpp.hpp"

namespace panda::detail
{
[[nodiscard]] auto mapPixelFormat(PixelFormat format) -> Result<vk::Format>;
[[nodiscard]] auto mapBufferUsage(BufferUsage usage) -> Result<vk::BufferUsageFlags>;
[[nodiscard]] auto mapTextureUsage(TextureUsage usage) -> Result<vk::ImageUsageFlags>;
}

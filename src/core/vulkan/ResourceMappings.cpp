// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "ResourceMappings.hpp"

#include <cstdint>
#include <expected>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/PixelFormat.hpp>
#include <panda/Texture.hpp>

namespace panda::detail
{
auto mapPixelFormat(PixelFormat format) -> Result<vk::Format>
{
    switch (format)
    {
    case PixelFormat::Rgba8Unorm:
        return vk::Format::eR8G8B8A8Unorm;
    case PixelFormat::Rgba8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case PixelFormat::Bgra8Unorm:
        return vk::Format::eB8G8R8A8Unorm;
    case PixelFormat::Bgra8Srgb:
        return vk::Format::eB8G8R8A8Srgb;
    case PixelFormat::Undefined:
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture format is undefined")};
    }
    return std::unexpected {makeError(ErrorCode::Unsupported, "Texture format is not in Panda's format catalog")};
}

auto mapBufferUsage(BufferUsage usage) -> Result<vk::BufferUsageFlags>
{
    constexpr auto transferSource = static_cast<std::uint32_t>(BufferUsage::TransferSource);
    constexpr auto transferDestination = static_cast<std::uint32_t>(BufferUsage::TransferDestination);
    constexpr auto storage = static_cast<std::uint32_t>(BufferUsage::Storage);
    constexpr auto known = transferSource | transferDestination | storage;
    const auto value = static_cast<std::uint32_t>(usage);
    if ((value & ~known) != 0)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Buffer usage contains unknown operations")};
    }
    auto mapped = vk::BufferUsageFlags {};
    if ((value & transferSource) != 0U)
    {
        mapped |= vk::BufferUsageFlagBits::eTransferSrc;
    }
    if ((value & transferDestination) != 0U)
    {
        mapped |= vk::BufferUsageFlagBits::eTransferDst;
    }
    if ((value & storage) != 0U)
    {
        mapped |= vk::BufferUsageFlagBits::eStorageBuffer;
    }
    return mapped;
}

auto mapTextureUsage(TextureUsage usage) -> Result<vk::ImageUsageFlags>
{
    constexpr auto transferDestination = static_cast<std::uint32_t>(TextureUsage::TransferDestination);
    constexpr auto sampled = static_cast<std::uint32_t>(TextureUsage::Sampled);
    constexpr auto known = transferDestination | sampled;
    const auto value = static_cast<std::uint32_t>(usage);
    if ((value & ~known) != 0U)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Texture usage contains unknown operations")};
    }
    auto mapped = vk::ImageUsageFlags {};
    if ((value & transferDestination) != 0U)
    {
        mapped |= vk::ImageUsageFlagBits::eTransferDst;
    }
    if ((value & sampled) != 0U)
    {
        mapped |= vk::ImageUsageFlagBits::eSampled;
    }
    return mapped;
}
}

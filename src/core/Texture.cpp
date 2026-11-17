#include <glm/ext/vector_uint2.hpp>
#include <memory>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <utility>

#include "vulkan/resources/TextureResource.hpp"

namespace panda
{
Texture::Texture() = default;
Texture::Texture(const Texture& other) = default;
auto Texture::operator=(const Texture& other) -> Texture& = default;
Texture::Texture(Texture&& other) noexcept = default;
auto Texture::operator=(Texture&& other) noexcept -> Texture& = default;
Texture::~Texture() = default;

Texture::Texture(std::shared_ptr<detail::TextureResource> resource)
    : _resource {std::move(resource)}
{
}

auto Texture::isValid() const noexcept -> bool
{
    return _resource != nullptr;
}

auto Texture::getExtent() const noexcept -> glm::uvec2
{
    if (!_resource)
    {
        return {};
    }
    const auto extent = _resource->getExtent();
    return {extent.width, extent.height};
}

auto Texture::getFormat() const noexcept -> PixelFormat
{
    return _resource ? _resource->getPublicFormat() : PixelFormat::Undefined;
}

auto Texture::getUsage() const noexcept -> TextureUsage
{
    return _resource ? _resource->getPublicUsage() : TextureUsage::None;
}

void Texture::reset() noexcept
{
    _resource.reset();
}
}

#include <cstddef>
#include <cstdint>
#include <functional>
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

auto Texture::operator==(const Texture& other) const noexcept -> bool
{
    return _resource == other._resource;
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

auto Texture::getType() const noexcept -> TextureType
{
    return _resource ? _resource->getPublicType() : TextureType::Texture2D;
}

auto Texture::getUsage() const noexcept -> TextureUsage
{
    return _resource ? _resource->getPublicUsage() : TextureUsage::None;
}

auto Texture::getMipLevels() const noexcept -> std::uint32_t
{
    return _resource ? _resource->getMipLevels() : 0U;
}

void Texture::reset() noexcept
{
    _resource.reset();
}
}

namespace std
{
auto hash<panda::Texture>::operator()(const panda::Texture& texture) const noexcept -> std::size_t
{
    return hash<const void*> {}(texture._resource.get());
}
}

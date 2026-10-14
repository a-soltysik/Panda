// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <memory>
#include <panda/Texture.hpp>
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

auto Texture::getExtent() const noexcept -> vk::Extent2D
{
    return _resource ? _resource->getExtent() : vk::Extent2D {};
}

auto Texture::getFormat() const noexcept -> vk::Format
{
    return _resource ? _resource->getFormat() : vk::Format::eUndefined;
}

auto Texture::getUsage() const noexcept -> vk::ImageUsageFlags
{
    return _resource ? _resource->getUsage() : vk::ImageUsageFlags {};
}

void Texture::reset() noexcept
{
    _resource.reset();
}
}

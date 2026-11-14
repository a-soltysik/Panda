// clang-format off
#include "vulkan/VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <memory>
#include <panda/OffscreenTarget.hpp>
#include <utility>

#include "vulkan/resources/OffscreenTargetResource.hpp"

namespace panda
{
OffscreenTarget::OffscreenTarget() = default;
OffscreenTarget::OffscreenTarget(const OffscreenTarget& other) = default;
auto OffscreenTarget::operator=(const OffscreenTarget& other) -> OffscreenTarget& = default;
OffscreenTarget::OffscreenTarget(OffscreenTarget&& other) noexcept = default;
auto OffscreenTarget::operator=(OffscreenTarget&& other) noexcept -> OffscreenTarget& = default;
OffscreenTarget::~OffscreenTarget() = default;

OffscreenTarget::OffscreenTarget(std::shared_ptr<detail::OffscreenTargetResource> resource)
    : _resource {std::move(resource)}
{
}

auto OffscreenTarget::isValid() const noexcept -> bool
{
    return _resource != nullptr;
}

auto OffscreenTarget::getExtent() const noexcept -> vk::Extent2D
{
    return _resource ? _resource->getExtent() : vk::Extent2D {};
}

auto OffscreenTarget::getFormat() const noexcept -> vk::Format
{
    return _resource ? _resource->getFormat() : vk::Format::eUndefined;
}

void OffscreenTarget::reset() noexcept
{
    _resource.reset();
}
}

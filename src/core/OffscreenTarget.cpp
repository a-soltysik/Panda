#include <glm/ext/vector_uint2.hpp>
#include <memory>
#include <panda/resources/OffscreenTarget.hpp>
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

auto OffscreenTarget::getExtent() const noexcept -> glm::uvec2
{
    if (!_resource)
    {
        return {};
    }
    const auto extent = _resource->getExtent();
    return {extent.width, extent.height};
}

void OffscreenTarget::reset() noexcept
{
    _resource.reset();
}
}

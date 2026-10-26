#include "external/ExternalSemaphoreResource.hpp"

#include <utility>
#include <vulkan/vulkan_raii.hpp>

namespace panda::detail
{
ExternalSemaphoreResource::ExternalSemaphoreResource(vk::raii::Semaphore semaphore)
    : _semaphore {std::move(semaphore)}
{
}

ExternalSemaphoreResource::~ExternalSemaphoreResource() = default;
}

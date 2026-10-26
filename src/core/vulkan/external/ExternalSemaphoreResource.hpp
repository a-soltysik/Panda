#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

namespace panda::detail
{
class ExternalSemaphoreResource final
{
public:
    explicit ExternalSemaphoreResource(vk::raii::Semaphore semaphore);
    ~ExternalSemaphoreResource();
    ExternalSemaphoreResource(const ExternalSemaphoreResource&) = delete;
    auto operator=(const ExternalSemaphoreResource&) -> ExternalSemaphoreResource& = delete;
    ExternalSemaphoreResource(ExternalSemaphoreResource&&) = delete;
    auto operator=(ExternalSemaphoreResource&&) -> ExternalSemaphoreResource& = delete;

private:
    vk::raii::Semaphore _semaphore {nullptr};
};
}

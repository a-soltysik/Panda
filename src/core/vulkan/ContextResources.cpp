// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "ContextResources.hpp"

#include <memory>
#include <optional>
#include <panda/DeviceInfo.hpp>

#include "InstanceResources.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalContext.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/Presentation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda::core::vulkan
{
auto ContextResources::loader() noexcept -> vk::raii::Context&
{
    return _loader;
}

auto ContextResources::instance() noexcept -> InstanceResources&
{
    return _instance;
}

auto ContextResources::physicalDevice() noexcept -> vk::raii::PhysicalDevice&
{
    return _physicalDevice;
}

auto ContextResources::device() noexcept -> vk::raii::Device&
{
    return _device;
}

auto ContextResources::memoryPool() noexcept -> std::unique_ptr<resources::MemoryPool>&
{
    return _memoryPool;
}

auto ContextResources::submissions() noexcept -> std::unique_ptr<detail::SubmissionQueue>&
{
    return _submissions;
}

auto ContextResources::uploads() noexcept -> std::optional<upload::UploadManager>&
{
    return _uploads;
}

auto ContextResources::externalContext() noexcept -> std::unique_ptr<detail::ExternalContext>&
{
    return _externalContext;
}

auto ContextResources::performanceManagerImplementation() noexcept -> std::unique_ptr<detail::PerformanceManagerImpl>&
{
    return _performanceManagerImplementation;
}

auto ContextResources::presentation() noexcept -> std::unique_ptr<Presentation>&
{
    return _presentation;
}

auto ContextResources::info() noexcept -> DeviceInfo&
{
    return _info;
}

}

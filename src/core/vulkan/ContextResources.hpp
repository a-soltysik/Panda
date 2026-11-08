#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <memory>
#include <optional>
#include <panda/Context.hpp>

#include "DeviceSelection.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalContext.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/Presentation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda::core::vulkan
{
struct InstanceResources
{
    InstanceExtensions extensions;
    vk::raii::Instance instance {nullptr};
    std::optional<vk::raii::DebugUtilsMessengerEXT> debugMessenger;
    std::optional<vk::raii::SurfaceKHR> surface;
};

class ContextResources final
{
public:
    ContextResources() = default;
    ~ContextResources() = default;
    ContextResources(const ContextResources&) = delete;
    auto operator=(const ContextResources&) -> ContextResources& = delete;
    ContextResources(ContextResources&&) = delete;
    auto operator=(ContextResources&&) -> ContextResources& = delete;

    [[nodiscard]] auto loader() noexcept -> vk::raii::Context&;
    [[nodiscard]] auto instance() noexcept -> InstanceResources&;
    [[nodiscard]] auto physicalDevice() noexcept -> vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() noexcept -> vk::raii::Device&;
    [[nodiscard]] auto memoryPool() noexcept -> std::unique_ptr<resources::MemoryPool>&;
    [[nodiscard]] auto submissions() noexcept -> std::unique_ptr<detail::SubmissionQueue>&;
    [[nodiscard]] auto uploads() noexcept -> std::optional<upload::UploadManager>&;
    [[nodiscard]] auto externalContext() noexcept -> std::unique_ptr<detail::ExternalContext>&;
    [[nodiscard]] auto performanceManagerImplementation() noexcept -> std::unique_ptr<detail::PerformanceManagerImpl>&;
    [[nodiscard]] auto presentation() noexcept -> std::unique_ptr<Presentation>&;
    [[nodiscard]] auto info() noexcept -> ContextDeviceInfo&;
    [[nodiscard]] auto info() const noexcept -> const ContextDeviceInfo&;

private:
    vk::raii::Context _loader {vkGetInstanceProcAddr};
    InstanceResources _instance;
    vk::raii::PhysicalDevice _physicalDevice {nullptr};
    vk::raii::Device _device {nullptr};
    std::unique_ptr<resources::MemoryPool> _memoryPool;
    std::unique_ptr<detail::SubmissionQueue> _submissions;
    std::optional<upload::UploadManager> _uploads;
    std::unique_ptr<detail::ExternalContext> _externalContext;
    std::unique_ptr<detail::PerformanceManagerImpl> _performanceManagerImplementation;
    std::unique_ptr<Presentation> _presentation;
    ContextDeviceInfo _info;
};
}

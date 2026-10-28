#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <string>
#include <utility>
#include <vector>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda::detail
{
class ExternalSemaphoreResource;
class ExternalBufferOwnershipTransferCommand;
class SubmissionQueue;
class BufferResource;
enum class ExternalBufferOwnershipTransferDirection : std::uint8_t;

struct ExternalSupport
{
    std::vector<std::string> deviceExtensions;
    vk::ExternalMemoryHandleTypeFlagBits memoryHandleType;
    ExternalMemoryHandleType publicHandleType;
    std::optional<std::pair<vk::ExternalSemaphoreHandleTypeFlagBits, ExternalSemaphoreHandleType>> semaphoreHandleType;
};

class ExternalContext final
{
public:
    struct CreateInfo
    {
        const vk::raii::PhysicalDevice& physicalDevice;
        const vk::raii::Device& device;
        core::vulkan::resources::MemoryPool& pool;
        std::optional<std::reference_wrapper<SubmissionQueue>> submissions;
        std::optional<core::vulkan::upload::UploadManager>& uploads;
        std::uint32_t queueFamily {};
        ExternalSupport support;
    };

    [[nodiscard]] static auto querySupport(const vk::raii::PhysicalDevice& physicalDevice, bool required)
        -> Result<std::optional<ExternalSupport>>;

    explicit ExternalContext(CreateInfo info);
    ~ExternalContext();
    ExternalContext(const ExternalContext&) = delete;
    auto operator=(const ExternalContext&) -> ExternalContext& = delete;
    ExternalContext(ExternalContext&&) = delete;
    auto operator=(ExternalContext&&) -> ExternalContext& = delete;

    [[nodiscard]] auto supportsSemaphoreExport() const noexcept -> bool;
    [[nodiscard]] auto createBuffer(const BufferDesc& description) -> Result<ExternalBuffer>;
    [[nodiscard]] auto release(const Buffer& buffer) -> Result<std::uint64_t>;
    [[nodiscard]] auto acquire(const Buffer& buffer, std::uint64_t externalSignalValue) -> Result<void>;
    [[nodiscard]] auto drain(std::uint64_t timeoutNanoseconds) -> Result<void>;

private:
    struct HandoffBuffer
    {
        Buffer owner;
        std::shared_ptr<BufferResource> resource;
        std::shared_ptr<ExternalSemaphoreResource> semaphore;
        std::reference_wrapper<SubmissionQueue> submissions;
    };

    [[nodiscard]] auto getHandoffBuffer(const Buffer& buffer) const -> Result<HandoffBuffer>;
    [[nodiscard]] auto submitHandoff(const HandoffBuffer& buffer,
                                     ExternalBufferOwnershipTransferDirection direction,
                                     std::uint64_t semaphoreValue) -> Result<void>;
    [[nodiscard]] auto prepareHandoff() -> Result<void>;
    [[nodiscard]] auto collectCompleted() -> Result<void>;
    [[nodiscard]] auto collectPending(SubmissionQueue& submissions) -> Result<void>;
    [[nodiscard]] auto createBufferWithMemory(const BufferDesc& description) -> Result<ExternalBuffer>;
    [[nodiscard]] auto createSemaphoreResource()
        -> Result<std::pair<ExternalSemaphoreHandle, std::shared_ptr<ExternalSemaphoreResource>>>;
    [[nodiscard]] auto exportMemoryHandle(vk::DeviceMemory memory) const -> Result<std::intptr_t>;
    [[nodiscard]] auto exportSemaphoreHandle(vk::Semaphore semaphore,
                                             vk::ExternalSemaphoreHandleTypeFlagBits handleType) const
        -> Result<std::intptr_t>;

    const vk::raii::PhysicalDevice& _physicalDevice;
    const vk::raii::Device& _device;
    core::vulkan::resources::MemoryPool& _pool;
    std::optional<std::reference_wrapper<SubmissionQueue>> _submissions;
    std::optional<core::vulkan::upload::UploadManager>& _uploads;
    std::uint32_t _queueFamily;
    ExternalSupport _support;
    VulkanDeviceUuid _deviceUuid {};
    std::deque<std::pair<std::uint64_t, std::unique_ptr<ExternalBufferOwnershipTransferCommand>>> _pending;
};
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class ExternalSemaphoreResource;
namespace buffer_resources = ::panda::core::vulkan::resources;

struct BufferResourceCreateInfo
{
    std::uint64_t size;
    vk::BufferUsageFlags usage;
    BufferMemory memory;
};

// Declaration order destroys the bound buffer before releasing its allocation.
// The context pool and device outlive this state, including every shared owner.
class BufferResource final
{
public:
    [[nodiscard]] static auto getResource(const Buffer& buffer) -> const std::shared_ptr<BufferResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     buffer_resources::MemoryPool& pool,
                                     const BufferDesc& description) -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] static auto createNative(const vk::raii::Device& device,
                                           buffer_resources::MemoryPool& pool,
                                           const BufferResourceCreateInfo& description)
        -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] static auto createExportable(const vk::raii::Device& device,
                                               buffer_resources::MemoryPool& pool,
                                               const BufferDesc& description,
                                               vk::ExternalMemoryHandleTypeFlagBits handleType)
        -> Result<std::shared_ptr<BufferResource>>;
    [[nodiscard]] auto getSize() const -> std::uint64_t;
    [[nodiscard]] auto getReservedSize() const -> std::uint64_t;
    [[nodiscard]] auto getNative() const -> vk::Buffer;
    [[nodiscard]] auto getNativeDevice() const -> vk::Device;
    [[nodiscard]] auto getNativeMemory() const -> vk::DeviceMemory;
    [[nodiscard]] auto getUsage() const -> vk::BufferUsageFlags;
    [[nodiscard]] auto belongsTo(const buffer_resources::MemoryPool& pool) const -> bool;

    [[nodiscard]] auto isExternallyOwned() const noexcept -> bool;
    void setExternallyOwned(bool externallyOwned) noexcept;
    void setExternalInterop(std::shared_ptr<ExternalSemaphoreResource> semaphore) noexcept;
    [[nodiscard]] auto isExportable() const noexcept -> bool;
    [[nodiscard]] auto getExternalSemaphore() const noexcept -> const std::shared_ptr<ExternalSemaphoreResource>&;
    [[nodiscard]] auto getExternalTimelineValue() const noexcept -> std::uint64_t;
    void setExternalTimelineValue(std::uint64_t value) noexcept;
    [[nodiscard]] auto getMappedBytes() const -> Result<std::span<std::byte>>;
    [[nodiscard]] auto writeMapped(std::uint64_t offset, std::span<const std::byte> bytes) const -> Result<void>;
    [[nodiscard]] auto invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>;

private:
    [[nodiscard]] static auto validateDescription(const BufferResourceCreateInfo& description) -> Result<void>;
    BufferResource(buffer_resources::MemoryPool& pool,
                   vk::raii::Buffer buffer,
                   const BufferResourceCreateInfo& description);
    [[nodiscard]] auto bind(const vk::raii::Device& device,
                            const BufferResourceCreateInfo& description,
                            vk::ExternalMemoryHandleTypeFlags handleTypes) -> Result<void>;
    [[nodiscard]] static auto createInternal(const vk::raii::Device& device,
                                             buffer_resources::MemoryPool& pool,
                                             const BufferResourceCreateInfo& description,
                                             vk::ExternalMemoryHandleTypeFlags handleTypes)
        -> Result<std::shared_ptr<BufferResource>>;

    buffer_resources::MemoryPool& _pool;
    buffer_resources::MemoryAllocation _allocation;
    vk::raii::Buffer _buffer;
    std::uint64_t _size;
    vk::BufferUsageFlags _usage;
    BufferMemory _intent;
    std::shared_ptr<ExternalSemaphoreResource> _externalSemaphore;
    std::uint64_t _externalTimelineValue {0};
    bool _externallyOwned {false};
    bool _exportable {false};
};
}

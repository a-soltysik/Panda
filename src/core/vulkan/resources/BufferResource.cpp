// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "BufferResource.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
using core::vulkan::resources::MemoryPool;
using core::vulkan::resources::MemoryRequest;

namespace
{
void configureMemory(MemoryRequest& request, BufferMemory intent)
{
    switch (intent)
    {
    case BufferMemory::Device:
        request.requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;
    case BufferMemory::Upload:
        request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
        request.preferredProperties = vk::MemoryPropertyFlagBits::eHostCoherent;
        break;
    case BufferMemory::Readback:
        request.requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible;
        request.preferredProperties = vk::MemoryPropertyFlagBits::eHostCached;
        break;
    }
}

auto makeMemoryRequest(const vk::raii::Device& device,
                       vk::Buffer buffer,
                       const BufferDesc& description,
                       vk::ExternalMemoryHandleTypeFlags handleTypes) -> MemoryRequest
{
    const auto requirements =
        device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
            vk::BufferMemoryRequirementsInfo2 {.buffer = buffer});
    auto request = MemoryRequest {.requirements = requirements.get<vk::MemoryRequirements2>().memoryRequirements,
                                  .dedicated = requirements.get<vk::MemoryDedicatedRequirements>(),
                                  .resource = vk::MemoryDedicatedAllocateInfo {.buffer = buffer},
                                  .exportHandleTypes = {},
                                  .requiredProperties = {},
                                  .preferredProperties = {}};
    configureMemory(request, description.memory);
    if (handleTypes)
    {
        request.exportHandleTypes = handleTypes;
        request.requiredProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    }
    return request;
}

auto validateExternalBufferSupport(const vk::raii::PhysicalDevice& physicalDevice,
                                   const BufferDesc& description,
                                   vk::ExternalMemoryHandleTypeFlagBits handleType) -> Result<void>
{
    const auto externalInfo =
        vk::PhysicalDeviceExternalBufferInfo {.usage = description.usage, .handleType = handleType};
    const auto properties = physicalDevice.getExternalBufferProperties(externalInfo).externalMemoryProperties;
    if (!(properties.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eExportable) ||
        !(properties.compatibleHandleTypes & handleType))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "The selected device cannot export this buffer usage")};
    }
    return {};
}

auto createNativeBuffer(const vk::raii::Device& device,
                        const BufferDesc& description,
                        vk::ExternalMemoryHandleTypeFlags handleTypes) -> Result<vk::raii::Buffer>
{
    const auto externalInfo = vk::ExternalMemoryBufferCreateInfo {.handleTypes = handleTypes};
    const auto info = vk::BufferCreateInfo {.pNext = handleTypes ? &externalInfo : nullptr,
                                            .size = description.size,
                                            .usage = description.usage,
                                            .sharingMode = vk::SharingMode::eExclusive};
    auto buffer = vk::Buffer {};
    const auto result = (*device).createBuffer(&info, nullptr, &buffer, *device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return Result<vk::raii::Buffer> {std::in_place, device, buffer};
}
}

auto BufferResource::validateDescription(const BufferDesc& description) -> Result<void>
{
    constexpr auto supported = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
                               vk::BufferUsageFlagBits::eUniformTexelBuffer |
                               vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
                               vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                               vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
    if (description.size == 0 || !description.usage ||
        (description.memory != BufferMemory::Device && description.memory != BufferMemory::Upload &&
         description.memory != BufferMemory::Readback))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid buffer size, usage or memory intent")};
    }
    if (description.usage & ~supported)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Buffer usage requires an unsupported device feature")};
    }
    return {};
}

auto BufferResource::getResource(const Buffer& buffer) -> const std::shared_ptr<BufferResource>&
{
    return buffer._resource;
}

auto BufferResource::create(const vk::raii::Device& device, MemoryPool& pool, const BufferDesc& description)
    -> Result<std::shared_ptr<BufferResource>>
{
    const auto valid = validateDescription(description);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    return createInternal(device, pool, description, {});
}

auto BufferResource::createExportable(const vk::raii::Device& device,
                                      MemoryPool& pool,
                                      const BufferDesc& description,
                                      vk::ExternalMemoryHandleTypeFlagBits handleType)
    -> Result<std::shared_ptr<BufferResource>>
{
    const auto handleTypes = vk::ExternalMemoryHandleTypeFlags {handleType};
    if (description.memory != BufferMemory::Device)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Exported buffers require device-local memory")};
    }
    const auto valid = validateDescription(description);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto supported = validateExternalBufferSupport(pool.getPhysicalDevice(), description, handleType);
        !supported)
    {
        return std::unexpected {supported.error()};
    }
    return createInternal(device, pool, description, handleTypes);
}

auto BufferResource::createInternal(const vk::raii::Device& device,
                                    MemoryPool& pool,
                                    const BufferDesc& description,
                                    vk::ExternalMemoryHandleTypeFlags handleTypes)
    -> Result<std::shared_ptr<BufferResource>>
{
    auto buffer = createNativeBuffer(device, description, handleTypes);
    if (!buffer)
    {
        return std::unexpected {std::move(buffer.error())};
    }
    auto resource = std::shared_ptr<BufferResource> {
        new BufferResource {pool, std::move(*buffer), description}
    };
    const auto bound = resource->bind(device, description, handleTypes);
    if (!bound)
    {
        return std::unexpected {bound.error()};
    }
    return resource;
}

BufferResource::BufferResource(MemoryPool& pool, vk::raii::Buffer buffer, const BufferDesc& description)
    : _pool {pool},
      _buffer {std::move(buffer)},
      _size {description.size},
      _usage {description.usage},
      _intent {description.memory}
{
}

auto BufferResource::bind(const vk::raii::Device& device,
                          const BufferDesc& description,
                          vk::ExternalMemoryHandleTypeFlags handleTypes) -> Result<void>
{
    const auto request = makeMemoryRequest(device, *_buffer, description, handleTypes);
    auto allocation = _pool.allocate(request);
    if (!allocation)
    {
        return std::unexpected {std::move(allocation.error())};
    }
    _allocation = std::move(*allocation);
    const auto info = vk::BindBufferMemoryInfo {.buffer = *_buffer,
                                                .memory = _allocation.getMemory(),
                                                .memoryOffset = _allocation.getRange().offset};
    const auto result = device.bindBufferMemory2(info);
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    return {};
}

auto BufferResource::getSize() const -> std::uint64_t
{
    return _size;
}

auto BufferResource::getReservedSize() const -> std::uint64_t
{
    return _allocation.getRange().size;
}

auto BufferResource::getNative() const -> vk::Buffer
{
    return *_buffer;
}

auto BufferResource::getNativeMemory() const -> vk::DeviceMemory
{
    return _allocation.getMemory();
}

auto BufferResource::getUsage() const -> vk::BufferUsageFlags
{
    return _usage;
}

auto BufferResource::belongsTo(const MemoryPool& pool) const -> bool
{
    return &_pool == &pool;
}

auto BufferResource::isExternallyOwned() const noexcept -> bool
{
    return _externallyOwned;
}

void BufferResource::setExternallyOwned(bool externallyOwned) noexcept
{
    _externallyOwned = externallyOwned;
}

void BufferResource::setExternalInterop(ExternalContext& context,
                                        std::shared_ptr<ExternalSemaphoreResource> semaphore) noexcept
{
    _externalContext.emplace(context);
    _externalSemaphore = std::move(semaphore);
}

auto BufferResource::getExternalContext() const noexcept -> std::optional<std::reference_wrapper<ExternalContext>>
{
    return _externalContext;
}

auto BufferResource::getExternalSemaphore() const noexcept -> const std::shared_ptr<ExternalSemaphoreResource>&
{
    return _externalSemaphore;
}

auto BufferResource::getExternalTimelineValue() const noexcept -> std::uint64_t
{
    return _externalTimelineValue;
}

void BufferResource::setExternalTimelineValue(std::uint64_t value) noexcept
{
    _externalTimelineValue = value;
}

auto BufferResource::getMappedBytes() const -> Result<std::span<std::byte>>
{
    return _allocation.getMappedBytes().and_then([&](std::span<std::byte> mapped) -> Result<std::span<std::byte>> {
        if (_size > mapped.size())
        {
            return std::unexpected {
                makeError(ErrorCode::BackendFailure, "Buffer payload exceeds its mapped allocation")};
        }
        return mapped.first(static_cast<std::size_t>(_size));
    });
}

auto BufferResource::writeMapped(std::uint64_t offset, std::span<const std::byte> bytes) const -> Result<void>
{
    if (_intent != BufferMemory::Upload || bytes.empty() || offset > _size || bytes.size() > _size - offset)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mapped upload bytes exceed staging range")};
    }
    return getMappedBytes().and_then([&](std::span<std::byte> mapped) {
        std::ranges::copy(bytes, mapped.begin() + static_cast<std::ptrdiff_t>(offset));
        return _allocation.flush(offset, bytes.size());
    });
}

auto BufferResource::invalidate(std::uint64_t offset, std::uint64_t size) const -> Result<void>
{
    return _allocation.invalidate(offset, size);
}
}

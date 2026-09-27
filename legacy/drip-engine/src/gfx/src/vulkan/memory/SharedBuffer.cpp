// clang-format off
#include <drip/common/log/LogMessageBuilder.hpp>
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "SharedBuffer.hpp"

#include <vulkan/vulkan.h>  // NOLINT(misc-include-cleaner)

#include <cstddef>
#include <drip/common/utils/Utils.hpp>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "drip/gfx/utils/format/ResultFormatter.hpp"  //NOLINT(misc-include-cleaner)
#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

SharedBuffer::SharedBuffer(const Device& deviceRef,
                           vk::DeviceSize instanceSize,
                           size_t instanceCount,
                           vk::MemoryPropertyFlags properties,
                           vk::DeviceSize minOffsetAlignment)
    : size {getAlignment(instanceSize, minOffsetAlignment) * instanceCount},
      buffer {createBuffer(deviceRef, size)},
      memory {allocateMemory(deviceRef, buffer, properties)},
      _device {deviceRef},
      _minOffsetAlignment {minOffsetAlignment}
{
    common::Expect(_device.logicalDevice.bindBufferMemory(buffer, memory, 0),
                   vk::Result::eSuccess,
                   "Failed to bind memory buffer");
    common::log::Info("Created new buffer [{}] with size: {}", static_cast<void*>(buffer), size);

    _bufferDestructor = std::make_unique<common::utils::ScopeGuard>([this] {
        _device.logicalDevice.destroy(buffer);
        _device.logicalDevice.freeMemory(memory);
    });
}

SharedBuffer::SharedBuffer(const Device& deviceRef, vk::DeviceSize bufferSize, vk::MemoryPropertyFlags properties)
    : SharedBuffer {deviceRef, bufferSize, 1, properties, 1}
{
}

SharedBuffer::~SharedBuffer()
{
    common::log::Info("Destroying shared buffer");
    common::ShouldBe(_device.logicalDevice.waitIdle(), vk::Result::eSuccess, "Failed to wait idle device");
}

auto SharedBuffer::createBuffer(const Device& device, vk::DeviceSize bufferSize) -> vk::Buffer
{
#ifdef WIN32
    static constexpr auto externalInfo =
        vk::ExternalMemoryBufferCreateInfo {.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32};
#else
    static constexpr auto externalInfo =
        vk::ExternalMemoryBufferCreateInfo {.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd};
#endif

    const auto bufferInfo = vk::BufferCreateInfo {.pNext = &externalInfo,
                                                  .size = bufferSize,
                                                  .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                                  .sharingMode = vk::SharingMode::eExclusive};

    return common::Expect(device.logicalDevice.createBuffer(bufferInfo),
                          vk::Result::eSuccess,
                          "Failed to create buffer")
        .result();
}

auto SharedBuffer::getBufferHandle() const -> Handle
{
#ifdef WIN32
    const auto getInfo =
        vk::MemoryGetWin32HandleInfoKHR {.memory = memory,
                                         .handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32};
    return common::Expect(_device.logicalDevice.getMemoryWin32HandleKHR(getInfo),
                          vk::Result::eSuccess,
                          "Failed to get memory handle")
        .result();
#else
    const auto getInfo =
        vk::MemoryGetFdInfoKHR {.memory = memory, .handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd};
    return common::Expect(_device.logicalDevice.getMemoryFdKHR(getInfo),
                          vk::Result::eSuccess,
                          "Failed to get memory handle")
        .result();
#endif
}

auto SharedBuffer::allocateMemory(const Device& device, vk::Buffer buffer, vk::MemoryPropertyFlags properties)
    -> vk::DeviceMemory
{
#ifdef WIN32
    static constexpr auto exportAllocationInfo =
        vk::ExportMemoryAllocateInfo {.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32};
#else
    static constexpr auto exportAllocationInfo =
        vk::ExportMemoryAllocateInfo {.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd};
#endif

    const auto memoryRequirements = device.logicalDevice.getBufferMemoryRequirements(buffer);
    const auto allocInfo = vk::MemoryAllocateInfo {
        .pNext = &exportAllocationInfo,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = common::Expect(device.findMemoryType(memoryRequirements.memoryTypeBits, properties),
                                          "Failed to find memory type")
                               .result()};

    return common::Expect(device.logicalDevice.allocateMemory(allocInfo),
                          vk::Result::eSuccess,
                          "Failed to allocated buffer memory")
        .result();
}

auto SharedBuffer::getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) noexcept
    -> vk::DeviceSize
{
    return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
}

auto SharedBuffer::getAlignment(vk::DeviceSize instanceSize) const noexcept -> vk::DeviceSize
{
    return getAlignment(instanceSize, _minOffsetAlignment);
}

auto SharedBuffer::getCurrentOffset() const noexcept -> vk::DeviceSize
{
    return _currentOffset;
}

auto SharedBuffer::getDescriptorInfo() const noexcept -> vk::DescriptorBufferInfo
{
    return getDescriptorInfoAt(size, 0);
}

auto SharedBuffer::getDescriptorInfoAt(vk::DeviceSize dataSize, vk::DeviceSize offset) const noexcept
    -> vk::DescriptorBufferInfo
{
    return {
        .buffer = buffer,
        .offset = offset,
        .range = dataSize,
    };
}

}

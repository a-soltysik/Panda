#include "panda/gfx/vulkan/Buffer.h"

namespace panda::gfx::vulkan
{
auto Buffer::copy(const Buffer& src, const Buffer& dst) -> void
{
    const auto allocInfo = vk::CommandBufferAllocateInfo {src._device.commandPool, vk::CommandBufferLevel::ePrimary, 1};
    const auto tmpCommandBuffers = expect(src._device.logicalDevice.allocateCommandBuffers(allocInfo),
                                          vk::Result::eSuccess,
                                          "Failed to allocate command buffer");

    const auto beginInfo = vk::CommandBufferBeginInfo {vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    expect(tmpCommandBuffers.front().begin(beginInfo), vk::Result::eSuccess, "Failed to begin command buffer");

    const auto copyRegion = vk::BufferCopy {{}, {}, src.size};
    tmpCommandBuffers.front().copyBuffer(src.buffer, dst.buffer, copyRegion);
    expect(tmpCommandBuffers.front().end(), vk::Result::eSuccess, "Failed to end command buffer");

    const auto submitInfo = vk::SubmitInfo {{}, {}, tmpCommandBuffers.front()};
    expect(src._device.graphicsQueue.submit(submitInfo), vk::Result::eSuccess, "Failed to submit command buffer");
    shouldBe(src._device.graphicsQueue.waitIdle(), vk::Result::eSuccess, "Failed to wait idle");
    src._device.logicalDevice.freeCommandBuffers(src._device.commandPool, tmpCommandBuffers);

    log::Info("Copied buffer [{}] to buffer [{}]", static_cast<void*>(src.buffer), static_cast<void*>(dst.buffer));
}

Buffer::Buffer(const Device& deviceRef,
               vk::DeviceSize instanceSize,
               size_t instanceCount,
               vk::BufferUsageFlags usage,
               vk::MemoryPropertyFlags properties,
               vk::DeviceSize minOffsetAlignment)
    : size {getAlignment(instanceSize, minOffsetAlignment) * instanceCount},
      buffer {createBuffer(deviceRef, size, usage)},
      memory {allocateMemory(deviceRef, buffer, properties)},
      _device {deviceRef},
      _minOffsetAlignment {minOffsetAlignment}
{
    expect(_device.logicalDevice.bindBufferMemory(buffer, memory, 0),
           vk::Result::eSuccess,
           "Failed to bind memory buffer");
    log::Info("Created new buffer [{}] with size: {}", static_cast<void*>(buffer), size);
}

Buffer::Buffer(const Device& deviceRef,
               vk::DeviceSize bufferSize,
               vk::BufferUsageFlags usage,
               vk::MemoryPropertyFlags properties)
    : Buffer {deviceRef, bufferSize, 1, usage, properties, 1}
{
}

Buffer::~Buffer() noexcept
{
    log::Info("Destroying buffer [{}]", static_cast<void*>(buffer));
    if (_mappedMemory != nullptr)
    {
        unmapWhole();
    }

    _device.logicalDevice.destroy(buffer);
    _device.logicalDevice.freeMemory(memory);
}

auto Buffer::flushWhole() const noexcept -> bool
{
    const auto mappedRange = vk::MappedMemoryRange {memory, 0, size};
    return shouldBe(_device.logicalDevice.flushMappedMemoryRanges(mappedRange),
                    vk::Result::eSuccess,
                    "Failed flushing memory");
}

auto Buffer::flush(vk::DeviceSize dataSize, vk::DeviceSize offset) const noexcept -> bool
{
    const auto mappedRange = vk::MappedMemoryRange {memory, offset, dataSize};
    return shouldBe(_device.logicalDevice.flushMappedMemoryRanges(mappedRange),
                    vk::Result::eSuccess,
                    "Failed flushing memory");
}

auto Buffer::mapWhole() noexcept -> void
{
    _mappedMemory = expect(_device.logicalDevice.mapMemory(memory, 0, size, {}),
                           vk::Result::eSuccess,
                           "Failed to map memory of vertex buffer");
}

auto Buffer::map(vk::DeviceSize dataSize, vk::DeviceSize offset) noexcept -> void
{
    _mappedMemory = expect(_device.logicalDevice.mapMemory(memory, offset, dataSize, {}),
                           vk::Result::eSuccess,
                           "Failed to map memory of vertex buffer");
}

auto Buffer::unmapWhole() noexcept -> void
{
    _device.logicalDevice.unmapMemory(memory);
    _mappedMemory = nullptr;
}

auto Buffer::createBuffer(const Device& device, vk::DeviceSize bufferSize, vk::BufferUsageFlags usage) -> vk::Buffer
{
    const auto bufferInfo = vk::BufferCreateInfo {{}, bufferSize, usage, vk::SharingMode::eExclusive};
    return expect(device.logicalDevice.createBuffer(bufferInfo), vk::Result::eSuccess, "Failed to create buffer");
}

auto Buffer::allocateMemory(const Device& device, vk::Buffer buffer, vk::MemoryPropertyFlags properties)
    -> vk::DeviceMemory
{
    const auto memoryRequirements = device.logicalDevice.getBufferMemoryRequirements(buffer);
    const auto allocInfo = vk::MemoryAllocateInfo {
        memoryRequirements.size,
        expect(device.findMemoryType(memoryRequirements.memoryTypeBits, properties), "Failed to find memory type")};
    return expect(device.logicalDevice.allocateMemory(allocInfo),
                  vk::Result::eSuccess,
                  "Failed to allocated buffer memory");
}

auto Buffer::getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) noexcept -> vk::DeviceSize
{
    return (instanceSize + minOffsetAlignment - 1u) & ~(minOffsetAlignment - 1u);
}

auto Buffer::getAlignment(vk::DeviceSize instanceSize) const noexcept -> vk::DeviceSize
{
    return getAlignment(instanceSize, _minOffsetAlignment);
}

auto Buffer::getCurrentOffset() const noexcept -> vk::DeviceSize
{
    return _currentOffset;
}

auto Buffer::getDescriptorInfo() const noexcept -> vk::DescriptorBufferInfo
{
    return getDescriptorInfoAt(size, 0);
}

auto Buffer::getDescriptorInfoAt(vk::DeviceSize dataSize, vk::DeviceSize offset) const noexcept
    -> vk::DescriptorBufferInfo
{
    return {
        buffer,
        offset,
        dataSize,
    };
}

}
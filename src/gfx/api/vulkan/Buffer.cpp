#include "Buffer.h"

namespace panda::gfx::vulkan
{
auto Buffer::copy(const Buffer& src, const Buffer& dst) -> void
{
    const auto allocInfo = vk::CommandBufferAllocateInfo {src.device.commandPool, vk::CommandBufferLevel::ePrimary, 1};
    const auto tmpCommandBuffers = expect(src.device.logicalDevice.allocateCommandBuffers(allocInfo),
                                          vk::Result::eSuccess,
                                          "Failed to allocate command buffer");

    const auto beginInfo = vk::CommandBufferBeginInfo {vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    expect(tmpCommandBuffers.front().begin(beginInfo), vk::Result::eSuccess, "Failed to begin command buffer");

    const auto copyRegion = vk::BufferCopy {{}, {}, src.size};
    tmpCommandBuffers.front().copyBuffer(src.buffer, dst.buffer, copyRegion);
    expect(tmpCommandBuffers.front().end(), vk::Result::eSuccess, "Failed to end command buffer");

    const auto submitInfo = vk::SubmitInfo {{}, {}, tmpCommandBuffers.front()};
    expect(src.device.graphicsQueue.submit(submitInfo), vk::Result::eSuccess, "Failed to submit command buffer");
    shouldBe(src.device.graphicsQueue.waitIdle(), vk::Result::eSuccess, "Failed to wait idle");
    src.device.logicalDevice.freeCommandBuffers(src.device.commandPool, tmpCommandBuffers);

    log::Info("Copied buffer [{}] to buffer [{}]", static_cast<void*>(src.buffer), static_cast<void*>(dst.buffer));
}

Buffer::Buffer(const Device& deviceRef,
               vk::DeviceSize bufferSize,
               vk::BufferUsageFlags usage,
               vk::MemoryPropertyFlags properties)
    : size {bufferSize},
      buffer {createBuffer(deviceRef, size, usage)},
      memory {allocateMemory(deviceRef, buffer, properties)},
      device {deviceRef}
{
    expect(device.logicalDevice.bindBufferMemory(buffer, memory, 0),
           vk::Result::eSuccess,
           "Failed to bind memory buffer");
    log::Info("Created new buffer [{}] with size: {}", static_cast<void*>(buffer), bufferSize);
}

Buffer::~Buffer() noexcept
{
    log::Info("Destroying buffer [{}]", static_cast<void*>(buffer));
    device.logicalDevice.destroy(buffer);
    device.logicalDevice.freeMemory(memory);
}

auto Buffer::createBuffer(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage) -> vk::Buffer
{
    const auto bufferInfo = vk::BufferCreateInfo {{}, size, usage, vk::SharingMode::eExclusive};
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

}
#pragma once

#include <ranges>

#include "Device.h"

namespace panda::gfx::vulkan
{
class Buffer
{
public:
    static auto copy(const Buffer& src, const Buffer& dst) -> void;

    Buffer(const Device& deviceRef,
           vk::DeviceSize bufferSize,
           vk::BufferUsageFlags usage,
           vk::MemoryPropertyFlags properties);

    template <std::ranges::range T>
    Buffer(const Device& deviceRef, T&& data, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
        : Buffer {deviceRef, std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>), usage, properties}
    {
        map(std::forward<T>(data));
    }

    PD_DELETE_ALL(Buffer);
    ~Buffer() noexcept;

    template <std::ranges::range T>
    auto map(T&& data) const -> void
    {
        using ValueT = std::ranges::range_value_t<T>;
        auto* mappedMemory =
            expect(device.logicalDevice.mapMemory(memory, 0, std::ranges::size(data) * sizeof(ValueT), {}),
                   vk::Result::eSuccess,
                   "Failed to map memory of vertex buffer");
        std::copy(std::ranges::begin(data), std::ranges::end(data), static_cast<ValueT*>(mappedMemory));
        device.logicalDevice.unmapMemory(memory);
    }

    const vk::DeviceSize size;
    const vk::Buffer buffer;
    const vk::DeviceMemory memory;

private:
    [[nodiscard]] static auto createBuffer(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage)
        -> vk::Buffer;
    [[nodiscard]] static auto allocateMemory(const Device& device,
                                             vk::Buffer buffer,
                                             vk::MemoryPropertyFlags properties) -> vk::DeviceMemory;

    const Device& device;
};
}

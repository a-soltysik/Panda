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

    Buffer(const Device& deviceRef,
           vk::DeviceSize instanceSize,
           size_t instanceCount,
           vk::BufferUsageFlags usage,
           vk::MemoryPropertyFlags properties,
           vk::DeviceSize minOffsetAlignment = 1);

    template <std::ranges::range T>
    Buffer(const Device& device,
           const T& data,
           vk::BufferUsageFlags usage,
           vk::MemoryPropertyFlags properties,
           vk::DeviceSize minOffsetAlignment = 1)
        : Buffer {device,
                  sizeof(std::ranges::range_value_t<T>),
                  std::ranges::size(data),
                  usage,
                  properties,
                  minOffsetAlignment}
    {
        mapWhole();
        write(data);
        unmapWhole();
    }

    PD_DELETE_ALL(Buffer);
    ~Buffer() noexcept;

    template <std::ranges::range T>
    requires(std::is_standard_layout_v<std::ranges::range_value_t<T>>)
    auto write(const T& data) -> vk::DeviceSize
    {
        return writeAt(data, _currentOffset);
    }

    template <typename T>
    requires(!std::ranges::range<T> && std::is_standard_layout_v<T>)
    auto write(const T& data) -> vk::DeviceSize
    {
        return writeAt(data, _currentOffset);
    }

    template <std::ranges::range T>
    requires(std::is_standard_layout_v<std::ranges::range_value_t<T>>)
    auto writeAt(const T& data, vk::DeviceSize offset) -> vk::DeviceSize
    {
        using ValueT = std::ranges::range_value_t<T>;
        const auto dataSize = std::ranges::size(data) * getAlignment(sizeof(ValueT), _minOffsetAlignment);
        expect(dataSize + _currentOffset <= size,
               fmt::format("Data with size: {} can't fit to buffer with size: {} and offset: {}",
                           dataSize,
                           size,
                           _currentOffset));

        std::copy(std::ranges::begin(data),
                  std::ranges::end(data),
                  reinterpret_cast<ValueT*>(reinterpret_cast<char*>(_mappedMemory) + offset));

        const auto previousOffset = _currentOffset;
        _currentOffset += dataSize;

        return previousOffset;
    }

    template <typename T>
    requires(!std::ranges::range<T> && std::is_standard_layout_v<T>)
    auto writeAt(const T& data, vk::DeviceSize offset) -> vk::DeviceSize
    {
        const auto dataSize = getAlignment(sizeof(T), _minOffsetAlignment);
        expect(dataSize + offset <= size,
               fmt::format("Data with size: {} can't fit to buffer with size: {} and offset: {}",
                           dataSize,
                           size,
                           _currentOffset));

        std::copy(reinterpret_cast<const char*>(&data),
                  reinterpret_cast<const char*>(&data) + sizeof(data),
                  reinterpret_cast<char*>(_mappedMemory) + offset);

        const auto previousOffset = _currentOffset;
        _currentOffset = offset + dataSize;

        return previousOffset;
    }

    auto flushWhole() const noexcept -> bool;
    auto flush(vk::DeviceSize dataSize, vk::DeviceSize offset = 0) const noexcept -> bool;

    auto mapWhole() noexcept -> void;
    auto map(vk::DeviceSize dataSize, vk::DeviceSize offset = 0) noexcept -> void;
    auto unmapWhole() noexcept -> void;

    [[nodiscard]] auto getAlignment(vk::DeviceSize instanceSize) const noexcept -> vk::DeviceSize;
    [[nodiscard]] auto getCurrentOffset() const noexcept -> vk::DeviceSize;
    [[nodiscard]] auto getDescriptorInfo() const noexcept -> vk::DescriptorBufferInfo;
    [[nodiscard]] auto getDescriptorInfoAt(vk::DeviceSize dataSize, vk::DeviceSize offset) const noexcept -> vk::DescriptorBufferInfo;

    const vk::DeviceSize size;
    const vk::Buffer buffer;
    const vk::DeviceMemory memory;

private:
    [[nodiscard]] static auto createBuffer(const Device& device, vk::DeviceSize bufferSize, vk::BufferUsageFlags usage)
        -> vk::Buffer;
    [[nodiscard]] static auto allocateMemory(const Device& device,
                                             vk::Buffer buffer,
                                             vk::MemoryPropertyFlags properties) -> vk::DeviceMemory;
    [[nodiscard]] static auto getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) noexcept
        -> vk::DeviceSize;

    const Device& _device;
    const vk::DeviceSize _minOffsetAlignment;
    void* _mappedMemory = nullptr;
    vk::DeviceSize _currentOffset = 0;
};
}

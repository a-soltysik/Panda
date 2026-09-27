#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <algorithm>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "vulkan/core/Device.hpp"

namespace drip::gfx
{
class Buffer
{
public:
    static void copy(const Buffer& src, const Buffer& dst);

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

    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    auto operator=(const Buffer&) = delete;
    auto operator=(Buffer&&) = delete;
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

    template <typename T>
    requires(std::is_standard_layout_v<T>)
    auto write(const T* data, size_t dataCount) -> vk::DeviceSize
    {
        return writeAt(data, dataCount, _currentOffset);
    }

    template <std::ranges::range T>
    requires(std::is_standard_layout_v<std::ranges::range_value_t<T>>)
    auto writeAt(const T& data, vk::DeviceSize offset) -> vk::DeviceSize
    {
        using ValueT = std::ranges::range_value_t<T>;
        const auto dataSize = std::ranges::size(data) * getAlignment(sizeof(ValueT), _minOffsetAlignment);
        common::Expect(dataSize + offset <= size,
                       "Data with size: {} can't fit to buffer with size: {} and offset: {}",
                       dataSize,
                       size,
                       offset);

        std::copy(std::ranges::begin(data),
                  std::ranges::end(data),
                  reinterpret_cast<ValueT*>(static_cast<char*>(_mappedMemory) + offset));

        const auto previousOffset = _currentOffset;
        _currentOffset += dataSize;

        return previousOffset;
    }

    template <typename T>
    requires(!std::ranges::range<T> && std::is_standard_layout_v<T>)
    auto writeAt(const T& data, vk::DeviceSize offset) -> vk::DeviceSize
    {
        const auto dataSize = getAlignment(sizeof(T), _minOffsetAlignment);
        common::Expect(dataSize + offset <= size,
                       "Data with size: {} can't fit to buffer with size: {} and offset: {}",
                       dataSize,
                       size,
                       _currentOffset);

        std::copy_n(reinterpret_cast<const char*>(&data), sizeof(data), static_cast<char*>(_mappedMemory) + offset);

        const auto previousOffset = _currentOffset;
        _currentOffset = offset + dataSize;

        return previousOffset;
    }

    template <typename T>
    requires(std::is_standard_layout_v<T>)
    auto writeAt(const T* data, size_t dataCount, vk::DeviceSize offset) -> vk::DeviceSize
    {
        const auto dataSize = getAlignment(dataCount * sizeof(T), _minOffsetAlignment);
        common::Expect(dataSize + offset <= size,
                       "Data with size: {} can't fit to buffer with size: {} and offset: {}",
                       dataSize,
                       size,
                       _currentOffset);

        std::copy_n(reinterpret_cast<const char*>(data),
                    dataCount * sizeof(T),
                    static_cast<char*>(_mappedMemory) + offset);

        const auto previousOffset = _currentOffset;
        _currentOffset = offset + dataSize;

        return previousOffset;
    }

    auto flushWhole() const noexcept -> bool;
    auto flush(vk::DeviceSize dataSize, vk::DeviceSize offset = 0) const noexcept -> bool;

    void mapWhole() noexcept;
    void map(vk::DeviceSize dataSize, vk::DeviceSize offset = 0) noexcept;
    void unmapWhole() noexcept;

    [[nodiscard]] auto getAlignment(vk::DeviceSize instanceSize) const noexcept -> vk::DeviceSize;
    [[nodiscard]] auto getCurrentOffset() const noexcept -> vk::DeviceSize;
    [[nodiscard]] auto getDescriptorInfo() const noexcept -> vk::DescriptorBufferInfo;
    [[nodiscard]] auto getDescriptorInfoAt(vk::DeviceSize dataSize, vk::DeviceSize offset) const noexcept
        -> vk::DescriptorBufferInfo;

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

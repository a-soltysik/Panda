#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <panda/Error.hpp>
#include <span>

#include "resources/memory/MemoryBlock.hpp"

namespace panda::test
{
class MemoryBlockMock
{
public:
    using NativeMemoryHandle = vk::DeviceMemory;
    using MemoryBlock = panda::core::vulkan::resources::MemoryBlock;
    using MemoryRange = panda::core::vulkan::resources::MemoryRange;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<void>,
                create,
                (const vk::raii::PhysicalDevice&, const vk::raii::Device&, std::uint64_t, std::uint32_t),
                ());
    MOCK_METHOD(panda::Result<void>,
                createDedicated,
                (const vk::raii::PhysicalDevice&,
                 const vk::raii::Device&,
                 vk::MemoryAllocateInfo,
                 const vk::MemoryDedicatedAllocateInfo&,
                 vk::ExternalMemoryHandleTypeFlags),
                ());
    MOCK_METHOD(panda::Result<MemoryRange>, allocate, (const MemoryBlock&, std::uint64_t, std::uint64_t), ());
    MOCK_METHOD(panda::Result<void>, release, (const MemoryBlock&, MemoryRange), ());
    MOCK_METHOD(panda::Result<std::span<std::byte>>, getMappedBytes, (const MemoryBlock&, MemoryRange), (const));
    MOCK_METHOD(panda::Result<void>, flush, (const MemoryBlock&, MemoryRange, std::uint64_t, std::uint64_t), (const));
    MOCK_METHOD(panda::Result<void>,
                invalidate,
                (const MemoryBlock&, MemoryRange, std::uint64_t, std::uint64_t),
                (const));
    // cppcheck-suppress syntaxError
    MOCK_METHOD(NativeMemoryHandle, getMemory, (const MemoryBlock&), (const));
    MOCK_METHOD(std::uint64_t, getFreeSize, (const MemoryBlock&), (const));
    MOCK_METHOD(std::uint64_t, getCapacity, (const MemoryBlock&), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

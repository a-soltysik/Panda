#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <span>

#include "BufferResource.hpp"

namespace panda::test
{
class BufferResourceMock
{
public:
    using BufferResource = panda::detail::BufferResource;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<void>,
                create,
                (const vk::raii::Device&, panda::core::vulkan::resources::MemoryPool&, const panda::BufferDesc&),
                ());
    // cppcheck cannot parse this gMock declaration with a resource reference.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(std::uint64_t, getSize, (const BufferResource&), (const));
    MOCK_METHOD(std::uint64_t, getReservedSize, (const panda::detail::BufferResource&), (const));
    MOCK_METHOD(vk::Buffer, getNative, (const panda::detail::BufferResource&), (const));
    MOCK_METHOD(vk::BufferUsageFlags, getUsage, (const panda::detail::BufferResource&), (const));
    MOCK_METHOD(bool,
                belongsTo,
                (const panda::detail::BufferResource&, const panda::core::vulkan::resources::MemoryPool&),
                (const));
    MOCK_METHOD(panda::Result<std::span<std::byte>>, getMappedBytes, (const panda::detail::BufferResource&), (const));
    MOCK_METHOD(panda::Result<void>,
                writeMapped,
                (const panda::detail::BufferResource&, std::uint64_t, std::span<const std::byte>),
                (const));
    MOCK_METHOD(panda::Result<void>,
                invalidate,
                (const panda::detail::BufferResource&, std::uint64_t, std::uint64_t),
                (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

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

#include "resources/memory/MemoryAllocation.hpp"

namespace panda::test
{
class MemoryAllocationMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    // cppcheck's gMock parser cannot model Vulkan and resource-handle returns.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(vk::DeviceMemory, getMemory, (std::uint64_t), (const));
    // cppcheck-suppress syntaxError
    MOCK_METHOD(panda::core::vulkan::resources::MemoryRange, getRange, (std::uint64_t), (const));
    MOCK_METHOD(panda::Result<std::span<std::byte>>, getMappedBytes, (std::uint64_t), (const));
    MOCK_METHOD(panda::Result<void>, flush, (std::uint64_t, std::uint64_t, std::uint64_t), (const));
    MOCK_METHOD(panda::Result<void>, invalidate, (std::uint64_t, std::uint64_t, std::uint64_t), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

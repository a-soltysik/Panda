#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstdint>
#include <panda/Error.hpp>

#include "resources/memory/MemoryPool.hpp"

namespace panda::test
{
class MemoryPoolMock
{
public:
    using MemoryPool = panda::core::vulkan::resources::MemoryPool;
    using MemoryPoolStats = panda::core::vulkan::resources::MemoryPoolStats;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<std::uint64_t>,
                allocate,
                (MemoryPool&, const panda::core::vulkan::resources::MemoryRequest&),
                ());
    MOCK_METHOD(void, release, (MemoryPool&, std::uint64_t), ());
    // cppcheck's gMock macro parser cannot model const-reference returns.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(const MemoryPoolStats&, getStats, (const MemoryPool&), (const));
    MOCK_METHOD(void, destroy, (MemoryPool&), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

#pragma once

#include <gmock/gmock.h>

#include <cstdint>
#include <panda/Error.hpp>

#include "resources/memory/RangeAllocator.hpp"

namespace panda::test
{
class RangeAllocatorMock
{
public:
    using MemoryRange = panda::core::vulkan::resources::MemoryRange;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(void, construct, (std::uint64_t), ());
    MOCK_METHOD(panda::Result<MemoryRange>, allocate, (std::uint64_t, std::uint64_t), ());
    MOCK_METHOD(panda::Result<void>, release, (MemoryRange), ());
    MOCK_METHOD(bool, isLive, (std::uint64_t, std::uint64_t), (const));
    MOCK_METHOD(std::uint64_t, getCapacity, (), (const));
    MOCK_METHOD(std::uint64_t, getFreeSize, (), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

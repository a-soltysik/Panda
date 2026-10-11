#pragma once

#include "VulkanHpp.hpp"

namespace panda::detail
{
// These memory types require allocation/device features outside the current
// supported resource contract. Selection and block creation share this policy.
inline constexpr auto unsupportedMemoryProperties = vk::MemoryPropertyFlagBits::eProtected |
                                                    vk::MemoryPropertyFlagBits::eLazilyAllocated |
                                                    vk::MemoryPropertyFlagBits::eDeviceCoherentAMD;
}

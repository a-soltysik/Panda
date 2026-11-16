#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "FakeHandle.hpp"

namespace panda::test
{
template <typename Function>
auto genericVulkanProc(Function function) noexcept -> PFN_vkVoidFunction
{
    // Vulkan specifies conversion between its entry-point pointer types.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}
}

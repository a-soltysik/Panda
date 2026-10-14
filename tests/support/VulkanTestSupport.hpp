#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace panda::test
{
template <typename Function>
auto genericVulkanProc(Function function) noexcept -> PFN_vkVoidFunction
{
    // Vulkan specifies conversion between its entry-point pointer types.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

template <typename Handle>
auto fakeVulkanHandle(std::uintptr_t value) -> Handle
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        // Presentation test IDs range through 901.
        static auto storage = std::array<std::max_align_t, 1024> {};
        // Opaque identity tokens pass through stubs without being dereferenced.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<Handle>(&storage.at(value));
    }
    else
    {
        return static_cast<Handle>(value);
    }
}
}

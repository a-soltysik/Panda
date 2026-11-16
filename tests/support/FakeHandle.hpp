#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace panda::test
{
template <typename Handle>
auto fakeHandle(std::uintptr_t value) -> Handle
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        // shortcut: fake IDs are limited to 1023; raise capacity when tests need more.
        static auto storage = std::array<std::max_align_t, 1024> {};
        // Opaque identities pass through stubs without being dereferenced.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<Handle>(&storage.at(value));
    }
    else
    {
        return static_cast<Handle>(value);
    }
}
}

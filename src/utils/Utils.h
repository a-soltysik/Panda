#pragma once

#include <functional>
#include <filesystem>

namespace panda::utils
{

template <typename T, typename... Rest>
auto hashCombine(size_t& seed, const T& v, const Rest&... rest) -> void
{
    seed ^= std::hash<T> {}(v) + 0x9e3779b9 + (seed << 6u) + (seed >> 2u);
    (hashCombine(seed, rest), ...);
}

}
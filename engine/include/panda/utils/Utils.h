#pragma once

#include <functional>

namespace panda::utils
{

template <auto>
using ConstexprChecker = void;

template <class... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

template <typename T, typename... Rest>
auto hashCombine(size_t& seed, const T& v, const Rest&... rest) -> void
{
    seed ^= std::hash<T> {}(v) + 0x9e3779b9 + (seed << 6u) + (seed >> 2u);
    (hashCombine(seed, rest), ...);
}

template <typename T, size_t N, template <typename, typename...> typename To = std::vector>
auto fromArray(const std::array<T, N>& array) -> To<T>
{
    return {array.cbegin(), array.cend()};
}

}

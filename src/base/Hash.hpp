#pragma once

#include <cstddef>
#include <functional>

namespace panda::detail
{
template <typename T>
void combineHash(std::size_t& seed, const T& value)
{
    static constexpr auto hashCombineConstant = std::size_t {0x9e3779b9U};
    static constexpr auto hashCombineLeftShift = 6U;
    static constexpr auto hashCombineRightShift = 2U;

    seed ^=
        std::hash<T> {}(value) + hashCombineConstant + (seed << hashCombineLeftShift) + (seed >> hashCombineRightShift);
}
}

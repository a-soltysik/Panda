#pragma once

#include <concepts>
#include <cstddef>
#include <glm/glm.hpp>
#include <utility>

#include "utils/Concepts.h"

namespace panda::gfx::vulkan
{

namespace internal
{

template <typename T, size_t N>
using ArrayOf = T[N];

[[nodiscard]] consteval auto roundUpToMultiple(size_t number, size_t base) -> size_t
{
    auto multiplier = size_t {1};
    while (number > multiplier * base)
    {
        multiplier++;
    }
    return multiplier * base;
}

static_assert(roundUpToMultiple(1, 1) == 1);
static_assert(roundUpToMultiple(1, 2) == 2);
static_assert(roundUpToMultiple(5, 16) == 16);
static_assert(roundUpToMultiple(20, 16) == 32);

}

template <typename T>
requires(!std::is_default_constructible_v<T> || !std::is_standard_layout_v<T>)
consteval auto alignment(T = {}) -> size_t;

template <utils::Scalar T>
[[nodiscard]] consteval auto alignment([[maybe_unused]] T dummy = {}) -> size_t
{
    return sizeof(T);
}

static_assert(alignment<uint32_t>() == 4);

template <template <glm::length_t, typename, glm::qualifier> typename V, glm::length_t L, typename T, glm::qualifier Q>
requires utils::Vec<V<L, T, Q>>
[[nodiscard]] consteval auto alignment([[maybe_unused]] V<L, T, Q> dummy = {}) -> size_t
{
    if constexpr (L == 2)
    {
        return 2 * sizeof(T);
    }
    return 4 * sizeof(T);
}

static_assert(alignment(glm::vec2 {}) == 8);
static_assert(alignment(glm::vec3 {}) == 16);
static_assert(alignment(glm::vec4 {}) == 16);

template <typename T, size_t N>
[[nodiscard]] consteval auto alignment([[maybe_unused]] const internal::ArrayOf<T, N> dummy = {}) -> size_t
{
    return internal::roundUpToMultiple(sizeof(T) * N, 16);
}

static_assert(alignment<uint32_t, 5>({}) == 32);

template <template <typename, size_t> typename A, typename T, size_t N>
[[nodiscard]] consteval auto alignment([[maybe_unused]] const std::array<T, N> dummy = {}) -> size_t
{
    return internal::roundUpToMultiple(sizeof(T) * N, 16);
}

static_assert(alignment<std::array, uint32_t, 5>() == 32);

template <template <glm::length_t, glm::length_t, typename, glm::qualifier> typename M,
          glm::length_t C,
          glm::length_t R,
          typename T,
          glm::qualifier Q>
requires utils::Mat<M<C, R, T, Q>>
[[nodiscard]] consteval auto alignment([[maybe_unused]] M<C, R, T, Q> dummy = {}) -> size_t
{
    return internal::roundUpToMultiple(alignment(glm::vec<R, T, Q>{}) * C, 16);
}

static_assert(alignment(glm::mat4{}) == 64);

}

#define PD_ALIGN(arg) alignas(alignment(arg {})) arg

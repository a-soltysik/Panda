#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <glm/detail/qualifier.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include "utils/Concepts.hpp"

namespace drip::gfx
{

namespace detail
{

template <typename T>
concept UboStruct = requires {
    { T::alignment() } -> std::convertible_to<size_t>;
};

template <typename T, size_t N>
using ArrayOf = T[N];

template <typename T>
concept ScalarOrVec = utils::Scalar<T> || utils::Vec<T>;

}

template <typename T>
struct AlignmentOf;

template <utils::Scalar T>
struct AlignmentOf<T>
{
    static constexpr size_t value = sizeof(T);
};

template <template <glm::length_t, typename, glm::qualifier> typename V, glm::length_t L, typename T, glm::qualifier Q>
requires utils::Vec<V<L, T, Q>>
struct AlignmentOf<V<L, T, Q>>
{
    static constexpr size_t value = (L == 2) ? (2 * sizeof(T)) : (4 * sizeof(T));
};

template <detail::UboStruct T>
struct AlignmentOf<T>
{
    static constexpr size_t value = T::alignment();
};

template <detail::ScalarOrVec T, size_t N>
struct AlignmentOf<std::array<T, N>>
{
    static constexpr size_t value = std::max(AlignmentOf<T>::value, static_cast<size_t>(16));
};

template <detail::UboStruct T, size_t N>
struct AlignmentOf<std::array<T, N>>
{
    static constexpr size_t value = AlignmentOf<T>::value;
};

template <template <glm::length_t, glm::length_t, typename, glm::qualifier> typename M,
          glm::length_t C,
          glm::length_t R,
          typename T,
          glm::qualifier Q>
requires utils::Mat<M<C, R, T, Q>>
struct AlignmentOf<M<C, R, T, Q>>
{
    static constexpr size_t value = AlignmentOf<std::array<glm::vec<R, T, Q>, static_cast<size_t>(C)>>::value;
};

template <typename T>
inline constexpr size_t AlignmentOfV = AlignmentOf<T>::value;

static_assert(AlignmentOfV<uint32_t> == 4);
static_assert(AlignmentOfV<float> == 4);
static_assert(AlignmentOfV<glm::vec2> == 8);
static_assert(AlignmentOfV<glm::vec3> == 16);
static_assert(AlignmentOfV<glm::vec4> == 16);
static_assert(AlignmentOfV<std::array<uint32_t, 5>> == 16);
static_assert(AlignmentOfV<glm::mat4> == 16);

template <typename... Types>
struct MaxAlignmentOf;

template <typename T>
struct MaxAlignmentOf<T>
{
    static constexpr size_t value = AlignmentOfV<T>;
};

template <typename T, typename... Rest>
struct MaxAlignmentOf<T, Rest...>
{
    static constexpr size_t value = std::max(AlignmentOfV<T>, MaxAlignmentOf<Rest...>::value);
};

template <typename... Types>
inline constexpr size_t MaxAlignmentOfV = MaxAlignmentOf<Types...>::value;

}

#define DRIP_EXPAND_IMPL(...) __VA_ARGS__
#define DRIP_EXPAND(x) DRIP_EXPAND_IMPL x

#define DRIP_EVAL0(...) __VA_ARGS__
#define DRIP_EVAL1(...) DRIP_EVAL0(DRIP_EVAL0(DRIP_EVAL0(__VA_ARGS__)))
#define DRIP_EVAL2(...) DRIP_EVAL1(DRIP_EVAL1(DRIP_EVAL1(__VA_ARGS__)))
#define DRIP_EVAL(...) DRIP_EVAL2(DRIP_EVAL2(DRIP_EVAL2(__VA_ARGS__)))

#define DRIP_GET_TYPE_IMPL(type, name) type
#define DRIP_GET_TYPE(x) DRIP_GET_TYPE_IMPL x
#define DRIP_DECLARE_MEMBER_IMPL(type, name) alignas(gfx::AlignmentOfV<type>) type name;
#define DRIP_DECLARE_MEMBER(x) DRIP_DECLARE_MEMBER_IMPL x

#define DRIP_ALIGNED_MEMBERS_IMPL_1(m1)                  \
    DRIP_DECLARE_MEMBER(m1)                              \
    static consteval auto alignment() noexcept -> size_t \
    {                                                    \
        return gfx::AlignmentOfV<DRIP_GET_TYPE(m1)>;     \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_2(m1, m2)                                \
    DRIP_DECLARE_MEMBER(m1)                                                \
    DRIP_DECLARE_MEMBER(m2)                                                \
    static consteval auto alignment() noexcept -> size_t                   \
    {                                                                      \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1), DRIP_GET_TYPE(m2)>; \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_3(m1, m2, m3)                                               \
    DRIP_DECLARE_MEMBER(m1)                                                                   \
    DRIP_DECLARE_MEMBER(m2)                                                                   \
    DRIP_DECLARE_MEMBER(m3)                                                                   \
    static consteval auto alignment() noexcept -> size_t                                      \
    {                                                                                         \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1), DRIP_GET_TYPE(m2), DRIP_GET_TYPE(m3)>; \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_4(m1, m2, m3, m4)                                                              \
    DRIP_DECLARE_MEMBER(m1)                                                                                      \
    DRIP_DECLARE_MEMBER(m2)                                                                                      \
    DRIP_DECLARE_MEMBER(m3)                                                                                      \
    DRIP_DECLARE_MEMBER(m4)                                                                                      \
    static consteval auto alignment() noexcept -> size_t                                                         \
    {                                                                                                            \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1), DRIP_GET_TYPE(m2), DRIP_GET_TYPE(m3), DRIP_GET_TYPE(m4)>; \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_5(m1, m2, m3, m4, m5)  \
    DRIP_DECLARE_MEMBER(m1)                              \
    DRIP_DECLARE_MEMBER(m2)                              \
    DRIP_DECLARE_MEMBER(m3)                              \
    DRIP_DECLARE_MEMBER(m4)                              \
    DRIP_DECLARE_MEMBER(m5)                              \
    static consteval auto alignment() noexcept -> size_t \
    {                                                    \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1),   \
                                    DRIP_GET_TYPE(m2),   \
                                    DRIP_GET_TYPE(m3),   \
                                    DRIP_GET_TYPE(m4),   \
                                    DRIP_GET_TYPE(m5)>;  \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_6(m1, m2, m3, m4, m5, m6) \
    DRIP_DECLARE_MEMBER(m1)                                 \
    DRIP_DECLARE_MEMBER(m2)                                 \
    DRIP_DECLARE_MEMBER(m3)                                 \
    DRIP_DECLARE_MEMBER(m4)                                 \
    DRIP_DECLARE_MEMBER(m5)                                 \
    DRIP_DECLARE_MEMBER(m6)                                 \
    static consteval auto alignment() noexcept -> size_t    \
    {                                                       \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1),      \
                                    DRIP_GET_TYPE(m2),      \
                                    DRIP_GET_TYPE(m3),      \
                                    DRIP_GET_TYPE(m4),      \
                                    DRIP_GET_TYPE(m5),      \
                                    DRIP_GET_TYPE(m6)>;     \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_7(m1, m2, m3, m4, m5, m6, m7) \
    DRIP_DECLARE_MEMBER(m1)                                     \
    DRIP_DECLARE_MEMBER(m2)                                     \
    DRIP_DECLARE_MEMBER(m3)                                     \
    DRIP_DECLARE_MEMBER(m4)                                     \
    DRIP_DECLARE_MEMBER(m5)                                     \
    DRIP_DECLARE_MEMBER(m6)                                     \
    DRIP_DECLARE_MEMBER(m7)                                     \
    static consteval auto alignment() noexcept -> size_t        \
    {                                                           \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1),          \
                                    DRIP_GET_TYPE(m2),          \
                                    DRIP_GET_TYPE(m3),          \
                                    DRIP_GET_TYPE(m4),          \
                                    DRIP_GET_TYPE(m5),          \
                                    DRIP_GET_TYPE(m6),          \
                                    DRIP_GET_TYPE(m7)>;         \
    }

#define DRIP_ALIGNED_MEMBERS_IMPL_8(m1, m2, m3, m4, m5, m6, m7, m8) \
    DRIP_DECLARE_MEMBER(m1)                                         \
    DRIP_DECLARE_MEMBER(m2)                                         \
    DRIP_DECLARE_MEMBER(m3)                                         \
    DRIP_DECLARE_MEMBER(m4)                                         \
    DRIP_DECLARE_MEMBER(m5)                                         \
    DRIP_DECLARE_MEMBER(m6)                                         \
    DRIP_DECLARE_MEMBER(m7)                                         \
    DRIP_DECLARE_MEMBER(m8)                                         \
    static consteval auto alignment() noexcept -> size_t            \
    {                                                               \
        return gfx::MaxAlignmentOfV<DRIP_GET_TYPE(m1),              \
                                    DRIP_GET_TYPE(m2),              \
                                    DRIP_GET_TYPE(m3),              \
                                    DRIP_GET_TYPE(m4),              \
                                    DRIP_GET_TYPE(m5),              \
                                    DRIP_GET_TYPE(m6),              \
                                    DRIP_GET_TYPE(m7),              \
                                    DRIP_GET_TYPE(m8)>;             \
    }

#define DRIP_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define DRIP_GET_MACRO_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME

#define DRIP_ALIGNED_MEMBERS(...)                         \
    DRIP_EVAL(DRIP_GET_MACRO(__VA_ARGS__,                 \
                             DRIP_ALIGNED_MEMBERS_IMPL_8, \
                             DRIP_ALIGNED_MEMBERS_IMPL_7, \
                             DRIP_ALIGNED_MEMBERS_IMPL_6, \
                             DRIP_ALIGNED_MEMBERS_IMPL_5, \
                             DRIP_ALIGNED_MEMBERS_IMPL_4, \
                             DRIP_ALIGNED_MEMBERS_IMPL_3, \
                             DRIP_ALIGNED_MEMBERS_IMPL_2, \
                             DRIP_ALIGNED_MEMBERS_IMPL_1)(__VA_ARGS__))

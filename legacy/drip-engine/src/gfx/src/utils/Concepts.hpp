#pragma once

#include <type_traits>

namespace drip::gfx::utils
{

namespace detail
{

template <class, template <glm::length_t, typename, glm::qualifier> class>
struct isVec : std::false_type
{
};

template <glm::length_t L, typename T, glm::qualifier Q, template <glm::length_t, typename, glm::qualifier> class V>
struct isVec<V<L, T, Q>, V> : std::true_type
{
};

template <class, template <glm::length_t, glm::length_t, typename, glm::qualifier> class>
struct isMat : std::false_type
{
};

template <glm::length_t C,
          glm::length_t R,
          typename T,
          glm::qualifier Q,
          template <glm::length_t, glm::length_t, typename, glm::qualifier> class M>
struct isMat<M<C, R, T, Q>, M> : std::true_type
{
};

}

template <typename T>
concept Scalar = std::is_scalar_v<T>;

template <typename T>
concept Vec = detail::isVec<T, glm::vec>::value;

template <typename T>
concept Mat = detail::isMat<T, glm::mat>::value;

}

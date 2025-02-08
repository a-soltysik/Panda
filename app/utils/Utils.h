#pragma once

#include <cmath>
#include <concepts>
#include <limits>
#include <ranges>

namespace app::utils
{
namespace detail
{
template <typename C>
struct to_helper
{
};

template <typename Container, std::ranges::range R>
requires std::convertible_to<std::ranges::range_value_t<R>, typename Container::value_type>
auto operator|(R&& r, to_helper<Container> /*unused*/) -> Container
{
    return Container {r.begin(), r.end()};
}
}

template <std::ranges::range Container>
requires(!std::ranges::view<Container>)
auto to()
{
    return detail::to_helper<Container> {};
}

template <std::floating_point T>
auto areEqual(T a, T b) -> bool
{
    return std::fabs(a - b) <= std::numeric_limits<T>::epsilon() * std::fmax(std::fabs(a), std::fabs(b));
}

template <std::floating_point T>
auto isZero(T a) -> bool
{
    return areEqual(a, static_cast<T>(0));
}

}

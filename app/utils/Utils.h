#pragma once

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

}

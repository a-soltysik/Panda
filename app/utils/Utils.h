#pragma once

#include <charconv>
#include <optional>
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

template <std::integral T>
constexpr auto maxLengthOfType() -> uint32_t
{
    return std::numeric_limits<T>::digits10 + 2;
}

template <std::integral T>
auto toNumber(std::string_view number) -> std::optional<T>
{
    auto result = T {};
    const auto [ptr, code] {std::from_chars(number.data(), number.data() + number.length(), result)};

    if (code == std::errc::invalid_argument || code == std::errc::result_out_of_range)
    {
        return {};
    }

    return result;
}

template <std::floating_point T>
auto toNumber(std::string_view number) -> std::optional<T>
{
    auto result = T {};
    const auto [ptr, code] {std::from_chars(number.data(), number.data() + number.length(), result)};

    if (code == std::errc::invalid_argument || code == std::errc::result_out_of_range)
    {
        return {};
    }

    return result;
}

template <std::integral T>
auto toString(T number) -> std::string
{
    auto buffer = std::array<char, maxLengthOfType<T>()> {};
    std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    auto result = std::string {buffer.cbegin(), buffer.cend()};
    result.erase(std::find(result.begin(), result.end(), '\0'), result.end());
    return result;
}

auto toString(std::floating_point auto number) -> std::string
{
    auto stream = std::stringstream {};
    stream << number;
    return stream.str();
}

auto toString(std::floating_point auto number, int32_t precision) -> std::string
{
    std::stringstream stream;
    stream << std::fixed << std::setprecision(precision) << number;
    return stream.str();
}

}

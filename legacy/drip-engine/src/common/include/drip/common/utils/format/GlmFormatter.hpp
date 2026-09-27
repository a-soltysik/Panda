#pragma once

#include <fmt/base.h>
#include <fmt/core.h>

#include <glm/detail/qualifier.hpp>
#include <glm/gtx/string_cast.hpp>

template <glm::length_t L, typename T, glm::qualifier Q>
struct fmt::formatter<glm::vec<L, T, Q>>
{
    constexpr auto parse(fmt::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const glm::vec<L, T, Q>& vec, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", glm::to_string(vec));
    }
};

#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <string_view>

#include "inputHandlers/MouseHandler.h"

template <>
struct fmt::formatter<app::MouseHandler::ButtonState> : formatter<std::string_view>
{
    template <typename FormatContext>
    [[nodiscard]] auto format(app::MouseHandler::ButtonState state, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(getStateName(state), ctx);
    }

    [[nodiscard]] static constexpr auto getStateName(app::MouseHandler::ButtonState state) noexcept -> std::string_view
    {
        using namespace std::string_view_literals;
        using enum app::MouseHandler::ButtonState;
        switch (state)
        {
        case JustPressed:
            return "JustPressed"sv;
        case Pressed:
            return "Pressed"sv;
        case JustReleased:
            return "JustReleased"sv;
        case Released:
            return "Released"sv;
        default:
            return "UnknownState"sv;
        }
    }
};

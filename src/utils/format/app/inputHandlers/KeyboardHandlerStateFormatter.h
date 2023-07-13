#pragma once

#include "app/inputHandlers/KeyboardHandler.h"

template <>
struct fmt::formatter<panda::app::KeyboardHandler::State> : formatter<std::string_view>
{
    template <typename FormatContext>
    [[nodiscard]] auto format(panda::app::KeyboardHandler::State state, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(getStateName(state), ctx);
    }

    [[nodiscard]] static constexpr auto getStateName(panda::app::KeyboardHandler::State state) noexcept -> std::string_view
    {
        using namespace std::string_view_literals;
        using enum panda::app::KeyboardHandler::State;
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

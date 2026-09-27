#pragma once

#include <fmt/base.h>

#include <boost/stacktrace/detail/frame_decl.hpp>
#include <boost/stacktrace/frame.hpp>
#include <boost/stacktrace/stacktrace.hpp>
#include <string_view>

template <>
struct fmt::formatter<boost::stacktrace::frame> : formatter<std::string_view>
{
    template <typename FormatContext>
    [[nodiscard]] auto format(const boost::stacktrace::frame& entry, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(boost::stacktrace::to_string(entry), ctx);
    }
};

template <>
struct fmt::formatter<boost::stacktrace::stacktrace> : formatter<std::string_view>
{
    template <typename FormatContext>
    [[nodiscard]] auto format(const boost::stacktrace::stacktrace& trace, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(boost::stacktrace::to_string(trace), ctx);
    }
};

#pragma once

#include <fmt/base.h>

#include <boost/exception/detail/exception_ptr.hpp>
#include <string_view>

template <>
struct fmt::formatter<boost::exception_ptr> : formatter<std::string_view>
{
    template <typename FormatContext>
    [[nodiscard]] auto format(const boost::exception_ptr& exception, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(boost::to_string(exception), ctx);
    }
};

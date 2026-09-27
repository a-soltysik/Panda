#pragma once

#include <string_view>

namespace panda::log::detail
{
auto relativeSourcePath(std::string_view path) noexcept -> std::string_view;
}

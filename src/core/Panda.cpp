#include "panda/Panda.hpp"

#include <string_view>

namespace panda
{

auto version() noexcept -> std::string_view
{
    constexpr auto versionText = std::string_view {PANDA_VERSION_TEXT};
    return versionText;
}

}

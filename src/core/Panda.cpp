#include "panda/Panda.hpp"

#include <string_view>

namespace panda
{

auto version() noexcept -> std::string_view
{
    constexpr std::string_view versionText {PANDA_VERSION_TEXT};
    return versionText;
}

}

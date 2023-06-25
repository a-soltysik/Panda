#pragma once

namespace panda::config
{

inline constexpr auto projectName = std::string_view {"Panda"};
inline constexpr auto targetName = std::string_view {"Panda.Engine"};

inline constexpr auto isDebug = bool{PD_DEBUG == 1};
inline constexpr auto isRelease = bool{PD_RELEASE == 1};

}

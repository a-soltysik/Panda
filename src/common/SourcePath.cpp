#include "SourcePath.hpp"

#include <string_view>

namespace panda::log::detail
{
auto relativeSourcePath(std::string_view path) noexcept -> std::string_view
{
    static constexpr std::string_view root {PANDA_SOURCE_DIRECTORY};
    if (path.size() < root.size())
    {
        return path;
    }
    for (auto index {0UZ}; index < root.size(); ++index)
    {
        const auto separator {root.at(index) == '/' && path.at(index) == '\\'};
        if (path.at(index) != root.at(index) && !separator)
        {
            return path;
        }
    }
    return path.substr(root.size());
}
}

#include <panda/CompletionPoint.hpp>

namespace panda
{
CompletionPoint::CompletionPoint() = default;

auto CompletionPoint::isValid() const noexcept -> bool
{
    return _contextId != 0 && _value != 0;
}
}

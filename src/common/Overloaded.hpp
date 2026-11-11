#pragma once

namespace panda::detail
{
template <class... Callables>
// NOLINTNEXTLINE(misc-multiple-inheritance) -- canonical overload-set idiom for std::visit.
struct Overloaded : Callables...
{
    using Callables::operator()...;
};

template <class... Callables>
Overloaded(Callables...) -> Overloaded<Callables...>;
}

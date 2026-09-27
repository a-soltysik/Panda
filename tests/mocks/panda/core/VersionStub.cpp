#include <panda/Panda.hpp>
#include <string_view>

#include "MockTest.hpp"
#include "VersionMock.hpp"

namespace panda
{
auto version() noexcept -> std::string_view
{
    return test::ScopedMock<test::VersionMock>::getActiveMock().version();
}
}

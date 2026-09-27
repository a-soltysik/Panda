#pragma once

#include <gmock/gmock.h>

#include <string_view>

namespace panda::test
{
class VersionMock
{
public:
    // Match version's noexcept contract; gMock's missing-default-value branch can throw.
    // NOLINTNEXTLINE(modernize-use-trailing-return-type,bugprone-exception-escape)
    MOCK_METHOD(std::string_view, version, (), (noexcept));
};
}

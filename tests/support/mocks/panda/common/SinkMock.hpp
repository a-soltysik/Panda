#pragma once

#include <gmock/gmock.h>

#include <expected>
#include <panda/Logger.hpp>

namespace panda::test
{
class SinkMock : public log::Sink
{
public:
    using Result = std::expected<void, log::SinkError>;

    // Match Sink's noexcept contract; gMock's generic missing-default-value branch can throw.
    // NOLINTBEGIN(modernize-use-trailing-return-type,bugprone-exception-escape)
    MOCK_METHOD(Result, write, (const log::Entry&), (noexcept, override));

    MOCK_METHOD(Result, flush, (), (noexcept, override));
    // NOLINTEND(modernize-use-trailing-return-type,bugprone-exception-escape)
};
}

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <panda/Logger.hpp>
#include <panda/Panda.hpp>
#include <string_view>

#include "LogCapture.hpp"
#include "MockTest.hpp"
#include "panda/core/VersionMock.hpp"

namespace
{
using ImplementationSubstitutionIntegration = panda::test::MockTest<panda::test::VersionMock>;
}

TEST_F(ImplementationSubstitutionIntegration, ReplacesOneArchiveMemberAndKeepsOtherImplementationsReal)
{
    constexpr auto selectedVersion = std::string_view {"test-selected-version"};
    EXPECT_CALL(getMock(), version()).WillOnce(testing::Return(selectedVersion));
    auto records = panda::test::LogRecords {};
    const auto registration = panda::test::ProcessSinkRegistration {records};

    const auto version = panda::version();
    EXPECT_EQ(version, selectedVersion);
    panda::log::info("Selected version: {}", version);

    ASSERT_EQ(records.entries.size(), std::size_t {1});
    EXPECT_EQ(records.entries.front().message, "Selected version: test-selected-version");
}

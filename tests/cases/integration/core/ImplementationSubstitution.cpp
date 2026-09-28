#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <panda/Logger.hpp>
#include <panda/Panda.hpp>
#include <string_view>

#include "LogCapture.hpp"
#include "ScopedMock.hpp"
#include "panda/core/VersionMock.hpp"

TEST(ImplementationSubstitutionIntegration, ReplacesOneArchiveMemberAndKeepsOtherImplementationsReal)
{
    auto versionMock = panda::test::ScopedMock<panda::test::VersionMock> {};
    constexpr auto selectedVersion = std::string_view {"test-selected-version"};
    EXPECT_CALL(versionMock, version()).WillOnce(testing::Return(selectedVersion));
    auto records = panda::test::LogRecords {};
    const auto registration = panda::test::ProcessSinkRegistration {records};

    const auto version = panda::version();
    EXPECT_EQ(version, selectedVersion);
    panda::log::info("Selected version: {}", version);

    ASSERT_EQ(records.entries.size(), std::size_t {1});
    EXPECT_EQ(records.entries.front().message, "Selected version: test-selected-version");
}

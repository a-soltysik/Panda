#include <gtest/gtest.h>

#include <panda/Panda.hpp>
#include <string_view>

TEST(Version, ReportsPackageVersion)
{
    constexpr std::string_view expected {PANDA_EXPECTED_VERSION};
    const auto actual {panda::version()};
    EXPECT_FALSE(actual.empty());
    EXPECT_TRUE(actual == expected);
}

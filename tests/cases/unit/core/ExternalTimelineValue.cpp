#include <gtest/gtest.h>

#include <cstdint>
#include <panda/ExternalTimelineValue.hpp>
#include <type_traits>

static_assert(!std::is_convertible_v<std::uint64_t, panda::ExternalTimelineValue>);
static_assert(!std::is_convertible_v<panda::ExternalTimelineValue, std::uint64_t>);

TEST(ExternalTimelineValueTest, PreservesValue)
{
    const auto value = panda::ExternalTimelineValue {42};

    EXPECT_EQ(value.getValue(), 42U);
}

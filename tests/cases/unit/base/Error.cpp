#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <panda/base/Error.hpp>
#include <source_location>

namespace
{
auto makeErrorAtHelper() -> panda::Error
{
    return panda::makeError(panda::ErrorCode::InvalidArgument, "invalid request");
}
}

TEST(Error, OwnsMessageAndNativeStatusAndPreservesExplicitSource)
{
    const auto source = std::source_location::current();
    const auto error = panda::makeError(panda::ErrorCode::BackendFailure,
                                        "backend unavailable",
                                        panda::NativeError {.api = "test API", .code = -17},
                                        source);

    EXPECT_EQ(error.code, panda::ErrorCode::BackendFailure);
    EXPECT_EQ(error.message, "backend unavailable");
    ASSERT_TRUE(error.native.has_value());
    const auto native = error.native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "test API");
    EXPECT_EQ(native.code, -17);
    EXPECT_EQ(error.source.file_name(), source.file_name());
    EXPECT_EQ(error.source.line(), source.line());
}

TEST(Error, DefaultSourceLocationPointsToTheCallingFunction)
{
    const auto error = makeErrorAtHelper();
    EXPECT_THAT(error.source.function_name(), testing::HasSubstr("makeErrorAtHelper"));
}

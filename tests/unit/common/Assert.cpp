#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Logger.hpp>
#include <source_location>
#include <string>
#include <tuple>

#include "LogCapture.hpp"

TEST(Assert, ChecksConditionsAndExtractsOwnedResults)
{
    panda::test::LogRecords records;
    const panda::test::ProcessSinkRegistration registration {records};
    EXPECT_TRUE(panda::shouldBe(true, "not logged"));
    EXPECT_FALSE(panda::shouldBe(false, "recoverable warning"));
    ASSERT_EQ(records.entries.size(), 1);
    EXPECT_EQ(records.entries.front().message, "recoverable warning");
    EXPECT_EQ(records.entries.front().level, panda::log::Level::Warning);
    panda::expect(true, "valid condition");
    EXPECT_EQ(panda::expect(std::expected<std::uint32_t, std::string> {42}, "required value"), 42);
    panda::expect(std::expected<void, std::string> {}, "required completion");
    auto value {
        panda::expect(std::expected<std::unique_ptr<std::uint32_t>, std::string> {std::make_unique<std::uint32_t>(7)},
                      "required owner")};
    EXPECT_EQ(*value, 7);
    auto optional {panda::expect(std::optional<std::unique_ptr<std::uint32_t>> {std::make_unique<std::uint32_t>(9)},
                                 "required optional owner")};
    EXPECT_EQ(*optional, 9);
    const auto copyable {std::expected<std::uint32_t, std::uint32_t> {3}};
    EXPECT_EQ(panda::expect(copyable, "copy lvalue"), 3);
    EXPECT_EQ(*copyable, 3);
}

TEST(Assert, NegatedComparisonsPreserveCallSites)
{
    panda::test::LogRecords records;
    const panda::test::ProcessSinkRegistration registration {records};
    auto value {std::uint32_t {7}};
    auto* pointer {&value};
    EXPECT_TRUE(panda::shouldNotBe(false, "false is allowed"));
    EXPECT_TRUE(panda::shouldBe(value, std::uint32_t {7}, "equal"));
    EXPECT_TRUE(panda::shouldNotBe(pointer, nullptr, "valid pointer"));
    EXPECT_TRUE(records.entries.empty());
    const auto line {std::source_location::current().line() + 1};
    EXPECT_FALSE(panda::shouldNotBe(true, "forbidden true"));
    ASSERT_EQ(records.entries.size(), 1);
    EXPECT_EQ(records.entries.front().location.line(), line);
    EXPECT_FALSE(panda::shouldBe(value, std::uint32_t {8}, "wrong value"));
    EXPECT_FALSE(panda::shouldNotBe(static_cast<std::uint32_t*>(nullptr), nullptr, "null pointer"));
    ASSERT_EQ(records.entries.size(), 3);
    EXPECT_EQ(records.entries.back().message, "null pointer");
    EXPECT_EQ(records.entries.back().level, panda::log::Level::Warning);
    panda::expectNot(false, "valid negation");
    EXPECT_EQ(panda::expect(value, std::uint32_t {7}, "equal value"), 7);
    EXPECT_EQ(panda::expectNot(pointer, nullptr, "valid pointer"), pointer);
    auto owner {panda::expectNot(std::make_unique<std::uint32_t>(9), nullptr, "valid owner")};
    EXPECT_EQ(*owner, 9);
}

TEST(Assert, InvokesPredicatesOnceAndPreservesOwnership)
{
    panda::test::LogRecords records;
    const panda::test::ProcessSinkRegistration registration {records};
    testing::MockFunction<bool(std::int32_t)> predicate;
    EXPECT_CALL(predicate, Call(2)).Times(2).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(predicate, Call(-2)).Times(2).WillRepeatedly(testing::Return(false));
    EXPECT_TRUE(panda::shouldBe(2, predicate.AsStdFunction(), "positive"));
    EXPECT_TRUE(panda::shouldNotBe(-2, predicate.AsStdFunction(), "nonpositive"));
    EXPECT_FALSE(panda::shouldBe(-2, predicate.AsStdFunction(), "must be positive"));
    EXPECT_FALSE(panda::shouldNotBe(2, predicate.AsStdFunction(), "must be nonpositive"));
    ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(&predicate));
    ASSERT_EQ(records.entries.size(), 2);
    EXPECT_CALL(predicate, Call(2)).WillOnce(testing::Return(true));
    EXPECT_CALL(predicate, Call(-2)).WillOnce(testing::Return(false));
    EXPECT_EQ(panda::expect(2, predicate.AsStdFunction(), "positive"), 2);
    EXPECT_EQ(panda::expectNot(-2, predicate.AsStdFunction(), "nonpositive"), -2);
    auto owner {panda::expect(
        std::make_unique<std::uint32_t>(5),
        [](const std::unique_ptr<std::uint32_t>& value) {
            return value && *value == 5;
        },
        "required owner")};
    EXPECT_EQ(*owner, 5);
}

TEST(AssertDeathTest, RejectsFalseCondition)
{
    ASSERT_DEATH(panda::expect(false, "fatal condition"), "Assert.cpp:.*fatal condition");
}

TEST(AssertDeathTest, ReportsExpectedErrorContext)
{
    ASSERT_DEATH(
        std::ignore = panda::expect(std::expected<std::uint32_t, std::string> {std::unexpect, "backend failure"},
                                    "fatal expected"),
        "Assert.cpp:.*fatal expected: backend failure");
}

TEST(AssertDeathTest, ReportsVoidExpectedErrorContext)
{
    ASSERT_DEATH(panda::expect(std::expected<void, std::string> {std::unexpect, "backend failure"}, "fatal void"),
                 "Assert.cpp:.*fatal void: backend failure");
}

TEST(AssertDeathTest, RejectsEmptyOptional)
{
    ASSERT_DEATH(std::ignore = panda::expect(std::optional<std::uint32_t> {}, "fatal optional"),
                 "Assert.cpp:.*fatal optional");
}

TEST(AssertDeathTest, RejectsForbiddenCondition)
{
    ASSERT_DEATH(panda::expectNot(true, "fatal negated"), "Assert.cpp:.*fatal negated");
}

TEST(AssertDeathTest, RejectsUnequalValues)
{
    ASSERT_DEATH(std::ignore = panda::expect(1, 2, "fatal equal"), "Assert.cpp:.*fatal equal");
}

TEST(AssertDeathTest, RejectsForbiddenPointer)
{
    ASSERT_DEATH(std::ignore = panda::expectNot(static_cast<std::uint32_t*>(nullptr), nullptr, "fatal pointer"),
                 "Assert.cpp:.*fatal pointer");
}

TEST(AssertDeathTest, RejectsFailedPredicate)
{
    ASSERT_DEATH(std::ignore = panda::expect(
                     1,
                     [](std::uint32_t value) {
                         return value == 2;
                     },
                     "fatal predicate"),
                 "Assert.cpp:.*fatal predicate");
}

TEST(AssertDeathTest, RejectsForbiddenPredicate)
{
    ASSERT_DEATH(std::ignore = panda::expectNot(
                     1,
                     [](std::uint32_t value) {
                         return value == 1;
                     },
                     "fatal forbidden"),
                 "Assert.cpp:.*fatal forbidden");
}

#ifdef PANDA_TEST_STACKTRACE
TEST(AssertDeathTest, IncludesStacktraceInFatalDiagnostic)
{
    EXPECT_DEATH(panda::expect(false, "fatal stacktrace test"), "STACKTRACE");
}
#endif

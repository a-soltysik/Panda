#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <panda/Logger.hpp>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "LogCapture.hpp"
#include "panda/common/SinkMock.hpp"

TEST(Logger, FiltersSeverityAndPreservesCallerMetadata)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {false};
    const auto sink = logger.addSink(panda::test::makeRecordingSink(records));
    panda::log::write(logger, panda::log::Level::Debug, "hidden {}", 1);
    EXPECT_TRUE(records.entries.empty());
    const auto line = std::source_location::current().line() + 1;
    panda::log::write(logger, panda::log::Level::Info, "value {}", 42);
    ASSERT_EQ(records.entries.size(), 1);
    EXPECT_EQ(records.entries.front().message, "value 42");
    EXPECT_EQ(records.entries.front().location.line(), line);
    EXPECT_TRUE(std::string_view {records.entries.front().location.file_name()}.ends_with("Logger.cpp"));
    EXPECT_TRUE(records.entries.front().time <= std::chrono::system_clock::now());
    EXPECT_EQ(records.flushes, 0);
    logger.write(panda::log::Level::Error, "flush now");
    EXPECT_EQ(records.flushes, 1);
    logger.setLevel(panda::log::Level::Off);
    logger.write(panda::log::Level::Error, "disabled");
    logger.write(panda::log::Level::Off, "never an entry");
    EXPECT_EQ(records.entries.size(), 2);
    logger.flush();
    EXPECT_EQ(records.flushes, 2);
    EXPECT_TRUE(logger.removeSink(sink));
    EXPECT_FALSE(logger.removeSink(sink));
    EXPECT_EQ(records.flushes, 3);
}

TEST(Logger, SerializesConcurrentWritesAndToleratesFailingSink)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {false};
    auto broken = std::make_unique<testing::StrictMock<panda::test::SinkMock>>();
    EXPECT_CALL(*broken, write(testing::_))
        .WillOnce(testing::Return(std::unexpected {panda::log::SinkError::WriteFailed}));
    EXPECT_CALL(*broken, flush())
        .Times(2)
        .WillRepeatedly(testing::Return(std::unexpected {panda::log::SinkError::FlushFailed}));
    std::ignore = logger.addSink(std::move(broken));
    std::ignore = logger.addSink(panda::test::makeRecordingSink(records));
    logger.write(panda::log::Level::Error, "survives failed sink");
    ASSERT_EQ(records.entries.size(), 1);
    EXPECT_EQ(records.flushes, 1);
    EXPECT_TRUE(logger.removeSink(0));
    static constexpr auto threadCount = std::size_t {4};
    static constexpr auto entriesPerThread = std::size_t {40};
    {
        auto workers = std::vector<std::jthread> {};
        workers.reserve(threadCount);
        for (auto worker = std::size_t {0}; worker < threadCount; ++worker)
        {
            workers.emplace_back([&logger, worker] {
                for (auto index = std::size_t {0}; index < entriesPerThread; ++index)
                {
                    panda::log::write(logger, panda::log::Level::Info, "{}:{}", worker, index);
                }
            });
        }
    }
    EXPECT_EQ(records.entries.size(), 1 + (threadCount * entriesPerThread));
    for (auto worker = std::size_t {0}; worker < threadCount; ++worker)
    {
        for (auto index = std::size_t {0}; index < entriesPerThread; ++index)
        {
            const auto message = std::format("{}:{}", worker, index);
            EXPECT_EQ(std::ranges::count(records.entries, message, &panda::log::Entry::message), 1);
        }
    }
}

TEST(LoggerDeathTest, RejectsNullSink)
{
    auto logger = panda::log::Logger {false};
    ASSERT_DEATH(std::ignore = logger.addSink(nullptr), "Logger.cpp:.*Cannot register a null log sink");
}

TEST(Logger, ExplicitStacktracePreservesMessageMetadataAndFiltering)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {false};
    std::ignore = logger.addSink(panda::test::makeRecordingSink(records));
    panda::log::writeWithStacktrace(logger, panda::log::Level::Debug, "hidden {}", 1);
    EXPECT_TRUE(records.entries.empty());
    const auto line = std::source_location::current().line() + 1;
    panda::log::writeWithStacktrace(logger, panda::log::Level::Error, "failure {}", 42);
    ASSERT_EQ(records.entries.size(), 1);
    EXPECT_TRUE(records.entries.front().message.starts_with("failure 42"));
    EXPECT_EQ(records.entries.front().location.line(), line);
    EXPECT_EQ(records.flushes, 1);
#ifdef PANDA_TEST_STACKTRACE
    EXPECT_TRUE(records.entries.front().message.contains("[STACKTRACE"));
#else
    EXPECT_EQ(records.entries.front().message, "failure 42");
#endif
}

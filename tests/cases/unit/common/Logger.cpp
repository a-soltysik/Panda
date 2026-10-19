#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <panda/Logger.hpp>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "LogCapture.hpp"
#include "panda/common/SinkMock.hpp"

namespace
{
constexpr auto concurrentThreadCount = std::size_t {4};
constexpr auto concurrentEntriesPerThread = std::size_t {40};

void writeWorkerEntries(panda::log::Logger& logger, std::size_t worker)
{
    for (auto index = std::size_t {0}; index < concurrentEntriesPerThread; ++index)
    {
        panda::log::write(logger, panda::log::Level::Info, "{}:{}", worker, index);
    }
}

void writeConcurrently(panda::log::Logger& logger)
{
    auto workers = std::vector<std::jthread> {};
    workers.reserve(concurrentThreadCount);
    for (auto worker = std::size_t {0}; worker < concurrentThreadCount; ++worker)
    {
        workers.emplace_back([&logger, worker] {
            writeWorkerEntries(logger, worker);
        });
    }
}

void expectConcurrentWrites(const panda::test::LogRecords& records)
{
    EXPECT_EQ(records.entries.size(), 1 + (concurrentThreadCount * concurrentEntriesPerThread));
    for (auto worker = std::size_t {0}; worker < concurrentThreadCount; ++worker)
    {
        for (auto index = std::size_t {0}; index < concurrentEntriesPerThread; ++index)
        {
            const auto message = std::format("{}:{}", worker, index);
            EXPECT_EQ(std::ranges::count(records.entries, message, &panda::log::Entry::message), 1);
        }
    }
}

auto temporaryLogPath() -> std::filesystem::path
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / std::format("panda-logger-{}.log", ticks);
}
}

TEST(Logger, FiltersSeverityAndPreservesCallerMetadata)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {};
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

TEST(Logger, StartsWithoutSinksAndWritesOnlyToApplicationSelectedSink)
{
    const auto path = temporaryLogPath();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    auto fileSink = panda::log::FileSink::open(path);
    ASSERT_TRUE(fileSink);
    auto logger = panda::log::Logger {};

    auto consoleOutput = std::ostringstream {};
    auto* originalBuffer = std::cerr.rdbuf(consoleOutput.rdbuf());
    logger.write(panda::log::Level::Info, "without sink");
    const auto sinkId = logger.addSink(std::move(*fileSink));
    logger.write(panda::log::Level::Info, "file entry");
    const auto sinkRemoved = logger.removeSink(sinkId);
    logger.write(panda::log::Level::Info, "after removal");
    std::cerr.rdbuf(originalBuffer);

    auto input = std::ifstream {path};
    const auto fileOpened = static_cast<bool>(input);
    const auto fileContents = std::string {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
    input.close();
    std::filesystem::remove(path, ignored);

    EXPECT_TRUE(fileOpened);
    EXPECT_TRUE(sinkRemoved);
    EXPECT_TRUE(consoleOutput.str().empty());
    EXPECT_TRUE(fileContents.contains("file entry"));
    EXPECT_FALSE(fileContents.contains("without sink"));
    EXPECT_FALSE(fileContents.contains("after removal"));
}

TEST(Logger, SerializesConcurrentWritesAndToleratesFailingSink)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {};
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
    writeConcurrently(logger);
    expectConcurrentWrites(records);
}

TEST(LoggerDeathTest, RejectsNullSink)
{
    auto logger = panda::log::Logger {};
    ASSERT_DEATH(std::ignore = logger.addSink(nullptr), "Logger.cpp:.*Cannot register a null log sink");
}

TEST(Logger, ExplicitStacktracePreservesMessageMetadataAndFiltering)
{
    auto records = panda::test::LogRecords {};
    auto logger = panda::log::Logger {};
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

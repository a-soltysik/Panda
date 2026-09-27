#include <gtest/gtest.h>

#include <fstream>
#include <ios>
#include <panda/Logger.hpp>
#include <source_location>
#include <sstream>
#include <string>

TEST(StreamSink, FormatsDiagnosticAndPreservesMetadata)
{
    auto stream = std::ostringstream {};
    auto sink = panda::log::StreamSink {stream};
    const auto entry = panda::log::Entry {.message = "stream diagnostic",
                                          .location = std::source_location::current(),
                                          .time = {},
                                          .level = panda::log::Level::Warning};
    ASSERT_TRUE(sink.write(entry).has_value());
    ASSERT_TRUE(sink.flush().has_value());
    EXPECT_TRUE(stream.str().contains("Z [WRN]"));
    EXPECT_TRUE(stream.str().contains("tests/unit/common/StreamSink.cpp:") ||
                stream.str().contains("tests\\unit\\common\\StreamSink.cpp:"));
    EXPECT_FALSE(stream.str().contains(entry.location.file_name()));
    EXPECT_TRUE(stream.str().ends_with("stream diagnostic\n"));
}

TEST(StreamSink, ReportsStreamFailureWithoutThrowing)
{
    auto stream = std::ostringstream {};
    stream.setstate(std::ios::badbit);
    auto sink = panda::log::StreamSink {stream};
    const auto written = sink.write({.message = "cannot write", .location = {}, .time = {}});
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), panda::log::SinkError::WriteFailed);
    const auto flushed = sink.flush();
    ASSERT_FALSE(flushed.has_value());
    EXPECT_EQ(flushed.error(), panda::log::SinkError::FlushFailed);
}

TEST(StreamSink, ConvertsDependencyExceptionsIntoResultErrors)
{
    auto unopened = std::ofstream {};
    // MSVC's iostate uses a signed integer for this standard-library bitmask.
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    unopened.exceptions(std::ios::badbit | std::ios::failbit);
    auto sink = panda::log::StreamSink {unopened};
    const auto written = sink.write({.message = "no open file", .location = {}, .time = {}});
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), panda::log::SinkError::WriteFailed);
    const auto flushed = sink.flush();
    ASSERT_FALSE(flushed.has_value());
    EXPECT_EQ(flushed.error(), panda::log::SinkError::FlushFailed);
}

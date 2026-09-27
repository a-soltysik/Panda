#include "LogCapture.hpp"

#include <gmock/gmock.h>

#include <memory>
#include <panda/Logger.hpp>
#include <tuple>

#include "panda/common/SinkMock.hpp"

namespace panda::test
{
auto makeRecordingSink(LogRecords& records) -> std::unique_ptr<log::Sink>
{
    auto sink {std::make_unique<testing::StrictMock<SinkMock>>()};
    EXPECT_CALL(*sink, write(testing::_)).WillRepeatedly([&records](const log::Entry& entry) -> SinkMock::Result {
        records.entries.push_back(entry);
        return {};
    });
    EXPECT_CALL(*sink, flush()).WillRepeatedly([&records]() -> SinkMock::Result {
        ++records.flushes;
        return {};
    });
    return sink;
}

ProcessSinkRegistration::ProcessSinkRegistration(LogRecords& records)
    : _identifier {log::Logger::instance().addSink(makeRecordingSink(records))}
{
}

ProcessSinkRegistration::~ProcessSinkRegistration()
{
    std::ignore = log::Logger::instance().removeSink(_identifier);
}
}

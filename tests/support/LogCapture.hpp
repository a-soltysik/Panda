#pragma once

#include <cstddef>
#include <memory>
#include <panda/Logger.hpp>
#include <vector>

namespace panda::test
{
struct LogRecords
{
    std::vector<log::Entry> entries;
    std::size_t flushes {0};
};

auto makeRecordingSink(LogRecords& records) -> std::unique_ptr<log::Sink>;

class ProcessSinkRegistration final
{
public:
    explicit ProcessSinkRegistration(LogRecords& records);

    ProcessSinkRegistration(const ProcessSinkRegistration&) = delete;

    ProcessSinkRegistration(ProcessSinkRegistration&&) = delete;

    auto operator=(const ProcessSinkRegistration&) -> ProcessSinkRegistration& = delete;

    auto operator=(ProcessSinkRegistration&&) -> ProcessSinkRegistration& = delete;

    ~ProcessSinkRegistration();

private:
    log::Logger::SinkId _identifier;
};
}

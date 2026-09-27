#pragma once

#include "drip/common/log/Logger.hpp"

namespace drip::common::log
{

class LogSink
{
public:
    LogSink() = default;
    LogSink(const LogSink&) = delete;
    LogSink(LogSink&&) = delete;
    auto operator=(const LogSink&) = delete;
    auto operator=(LogSink&&) = delete;
    virtual ~LogSink() = default;

    virtual void write(const Logger::Entry& entry) = 0;
    virtual void flush() = 0;
};
}

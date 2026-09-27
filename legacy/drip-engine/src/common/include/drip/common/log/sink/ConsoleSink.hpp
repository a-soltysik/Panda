#pragma once

#include "LogSink.hpp"

namespace drip::common::log
{
class ConsoleSink final : public LogSink
{
public:
    void write(const Logger::Entry& entry) override;
    void flush() override;
};
}

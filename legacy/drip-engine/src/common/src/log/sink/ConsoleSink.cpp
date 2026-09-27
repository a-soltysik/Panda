#include "drip/common/log/sink/ConsoleSink.hpp"

#include <fmt/base.h>

#include "drip/common/log/Logger.hpp"

namespace drip::common::log
{

void ConsoleSink::write(const Logger::Entry& entry)
{
    fmt::println("{}", entry);
}

void ConsoleSink::flush() { }

}

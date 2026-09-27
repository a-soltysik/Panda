#include "drip/common/log/LogMessageBuilder.hpp"

#include <fmt/base.h>
#include <fmt/format.h>

#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/stacktrace/stacktrace.hpp>
#include <exception>
#include <source_location>
#include <string>
#include <utility>

#include "drip/common/log/Logger.hpp"
#include "drip/common/utils/format/ExceptionFormatter.hpp"   // NOLINT(misc-include-cleaner)
#include "drip/common/utils/format/StacktraceFormatter.hpp"  // NOLINT(misc-include-cleaner)

namespace drip::common::log::detail
{

LogMessageBuilder::LogMessageBuilder(std::string message,
                                     const std::source_location& location,
                                     Logger::Level level,
                                     bool enabled)
    : _message(std::move(message)),
      _location(location),
      _level(level),
      _enabled(enabled)
{
}

LogMessageBuilder::~LogMessageBuilder()
{
    if (!_asString)
    {
        log();
    }
}

auto LogMessageBuilder::withCurrentException(boost::exception_ptr exception) -> LogMessageBuilder&
{
    if (_enabled)
    {
        _message += fmt::format("\n{}", exception);
    }
    return *this;
}

auto LogMessageBuilder::withStacktrace(boost::stacktrace::stacktrace stacktrace) -> LogMessageBuilder&
{
    if (_enabled)
    {
        _message += fmt::format("\n{}", stacktrace);
    }
    return *this;
}

auto LogMessageBuilder::withStacktraceFromCurrentException(boost::stacktrace::stacktrace stacktrace)
    -> LogMessageBuilder&
{
    return withStacktrace(std::move(stacktrace));
}

auto LogMessageBuilder::asString() -> std::string
{
    _asString = true;
    return _message;
}

void LogMessageBuilder::log() noexcept
{
    if (!_enabled)
    {
        return;
    }

    try
    {
        Logger::instance().log(_level, std::move(_message), _location);
    }
    catch (...)
    {
        Logger::instance().raiseException(std::current_exception());
    }
}
}

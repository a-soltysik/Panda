#include "drip/common/log/sink/FileSink.hpp"

#include <fmt/chrono.h>  // NOLINT(misc-include-cleaner)
#include <fmt/format.h>
#include <fmt/os.h>

#include <boost/exception/detail/exception_ptr.hpp>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>

#include "drip/common/log/Logger.hpp"

namespace drip::common::log
{

FileSink::FileSink(const std::filesystem::path& logDirectory)
    : _file(openFile(logDirectory))
{
}

FileSink::~FileSink()
{
    try
    {
        FileSink::flush();
        if (_file)
        {
            _file->close();
        }
    }
    catch (...)
    {
        Logger::instance().raiseException(boost::current_exception());
    }
}

auto FileSink::openFile(const std::filesystem::path& logDirectory) -> std::optional<fmt::ostream>
{
    try
    {
        std::filesystem::create_directories(logDirectory);

        const auto time = std::chrono::system_clock::now();
        const auto filename =
            logDirectory / fmt::format("{:%F_%H_%M_%S}.log", std::chrono::floor<std::chrono::seconds>(time));

        return std::make_optional<fmt::ostream>(fmt::output_file(filename.string()));
    }
    catch (...)
    {
        Logger::instance().raiseException(boost::current_exception());
        return {};
    }
}

void FileSink::write(const Logger::Entry& entry)
{
    if (!_file)
    {
        return;
    }

    const auto lock = std::scoped_lock {_bufferMutex};

    _buffer.push_back(entry);
}

void FileSink::flush()
{
    if (!_file)
    {
        return;
    }

    try
    {
        const auto lock = std::scoped_lock {_bufferMutex};
        for (const auto& entry : _buffer)
        {
            _file->print("{}\n", entry);
        }
        _file->flush();
        _buffer.clear();
    }
    catch (...)
    {
        _file = std::nullopt;
        Logger::instance().raiseException(boost::current_exception());
    }
}

}

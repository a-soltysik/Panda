#pragma once

#include <fmt/os.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LogSink.hpp"

namespace drip::common::log
{

class FileSink : public LogSink
{
public:
    explicit FileSink(const std::filesystem::path& logDirectory = "logs");
    ~FileSink() override;

    FileSink(const FileSink&) = delete;
    auto operator=(const FileSink&) -> FileSink& = delete;
    FileSink(FileSink&&) = delete;
    auto operator=(FileSink&&) -> FileSink& = delete;

    void write(const Logger::Entry& entry) override;
    void flush() override;

private:
    static auto openFile(const std::filesystem::path& logDirectory) -> std::optional<fmt::ostream>;

    std::mutex _bufferMutex;
    std::optional<fmt::ostream> _file;
    std::vector<Logger::Entry> _buffer;
};

}

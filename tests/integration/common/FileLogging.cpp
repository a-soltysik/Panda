#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <panda/Assert.hpp>
#include <panda/Logger.hpp>
#include <sstream>
#include <system_error>
#include <tuple>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::filesystem::create_directory(_path);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;

    TemporaryDirectory(TemporaryDirectory&&) = delete;

    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;

    auto operator=(TemporaryDirectory&&) -> TemporaryDirectory& = delete;

    ~TemporaryDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::ignore = std::filesystem::remove_all(_path, error);
            panda::expect(!error, "Cannot remove temporary test directory");
        }
        catch (...)
        {
            panda::panic("Temporary test directory cleanup threw an exception");
        }
    }

    [[nodiscard]] auto getPath() const -> const std::filesystem::path&
    {
        return _path;
    }

private:
    std::filesystem::path _path {
        std::filesystem::temp_directory_path() /
        std::format("panda-logger-{}", std::chrono::steady_clock::now().time_since_epoch().count())};
};
}

TEST(FileLoggingIntegration, AppendsEntriesAndReportsOpenFailure)
{
    const TemporaryDirectory directory;
    const auto path {directory.getPath() / "output.log"};
    for (auto iteration {std::size_t {0}}; iteration < 2; ++iteration)
    {
        auto sink {panda::log::FileSink::open(path)};
        ASSERT_TRUE(sink.has_value());
        const panda::log::Entry entry {.message = std::format("entry {}", iteration), .location = {}, .time = {}};
        ASSERT_TRUE((*sink)->write(entry).has_value());
        ASSERT_TRUE((*sink)->flush().has_value());
    }
    const std::ifstream input {path};
    std::stringstream contents;
    contents << input.rdbuf();
    EXPECT_TRUE(contents.str().contains("entry 0"));
    EXPECT_TRUE(contents.str().contains("entry 1"));
    const auto missing {directory.getPath() / "missing" / "output.log"};
    const auto failure {panda::log::FileSink::open(missing)};
    ASSERT_FALSE(failure.has_value());
    EXPECT_TRUE(failure.error().contains(missing.string()));
}

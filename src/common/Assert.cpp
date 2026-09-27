#include "panda/Assert.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FunctionName.hpp"
#include "SourcePath.hpp"
#include "Stacktrace.hpp"
#include "panda/Logger.hpp"

namespace panda
{
namespace
{
void writeFatalText(std::string_view text) noexcept
{
    if (!text.empty())
    {
        std::ignore = std::fwrite(text.data(), sizeof(char), text.size(), stderr);
    }
}
}

[[noreturn]] void panic(std::string_view message, std::source_location location) noexcept
{
    std::array<char, std::numeric_limits<std::uint_least32_t>::digits10 + 1> line {};
    const auto converted {std::to_chars(line.data(), std::to_address(line.end()), location.line())};
    const log::detail::FunctionName functionName {location.function_name()};
    writeFatalText("[FATAL] ");
    writeFatalText(log::detail::relativeSourcePath(location.file_name()));
    writeFatalText(":");
    writeFatalText(std::string_view {line.data(), converted.ptr});
    writeFatalText(" (");
    writeFatalText(functionName.view());
    writeFatalText("): ");
    writeFatalText(message);
    writeFatalText("\n");
    std::ignore = std::fflush(stderr);
    try
    {
        writeFatalText(log::detail::stacktraceText());
    }
    catch (...)
    {
        writeFatalText("[STACKTRACE unavailable]\n");
    }
    std::ignore = std::fflush(stderr);
    std::abort();
}

auto shouldBe(bool condition, std::string_view message, std::source_location location) -> bool
{
    if (!condition)
    {
        log::Logger::instance().write(log::Level::Warning, std::string {message}, location);
    }
    return condition;
}

void expect(bool condition, std::string_view message, std::source_location location) noexcept
{
    if (!condition)
    {
        panic(message, location);
    }
}

auto shouldNotBe(bool condition, std::string_view message, std::source_location location) -> bool
{
    return shouldBe(!condition, message, location);
}

void expectNot(bool condition, std::string_view message, std::source_location location) noexcept
{
    expect(!condition, message, location);
}

}

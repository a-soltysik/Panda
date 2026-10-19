#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace panda_bench
{
inline constexpr std::uint32_t defaultSmokeWindowWidth {1280};
inline constexpr std::uint32_t defaultSmokeWindowHeight {720};

struct Options
{
    std::string profile {"smoke"};
    std::filesystem::path outputDirectory {"results"};
    std::uint32_t windowWidth {defaultSmokeWindowWidth};
    std::uint32_t windowHeight {defaultSmokeWindowHeight};
    std::chrono::seconds warmupDuration {1};
    std::chrono::seconds measurementDuration {3};
    std::size_t runCount {1};
    bool showHelp {false};
};

[[nodiscard]] auto parseArguments(int argumentCount, char** arguments) -> std::expected<Options, std::string>;
[[nodiscard]] auto usage() -> std::string;
}

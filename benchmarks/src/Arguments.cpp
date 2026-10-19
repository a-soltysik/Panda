#include <argparse/argparse.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <panda_bench/Arguments.hpp>
#include <string>

namespace panda_bench
{
namespace
{
constexpr auto maximumWarmupDuration = std::chrono::hours {1};
constexpr auto maximumMeasurementDuration = std::chrono::hours {24};
constexpr auto maximumRunCount = std::int64_t {100};

struct ParsingState
{
    Options options;
    std::string outputDirectory {"results"};
    std::int64_t warmupSeconds {1};
    std::int64_t measurementSeconds {3};
    std::int64_t runCount {1};
};

void addProfileOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--profile")
        .help("Workload profile; currently smoke only")
        .default_value(std::string {"smoke"})
        .nargs(1)
        .required()
        .store_into(state.options.profile);
    parser.add_argument("--output")
        .help("Directory for the JSON report")
        .default_value(std::string {"results"})
        .nargs(1)
        .required()
        .store_into(state.outputDirectory);
}

void addTimingOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--warmup-seconds")
        .help("Warmup duration for each run")
        .default_value(std::int64_t {1})
        .nargs(1)
        .required()
        .store_into(state.warmupSeconds);
    parser.add_argument("--measure-seconds")
        .help("Measurement duration for each run")
        .default_value(std::int64_t {3})
        .nargs(1)
        .required()
        .store_into(state.measurementSeconds);
    parser.add_argument("--runs")
        .help("Number of independent runs")
        .default_value(std::int64_t {1})
        .nargs(1)
        .required()
        .store_into(state.runCount);
}

void addWindowOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--width")
        .help("Smoke window width in pixels")
        .default_value(state.options.windowWidth)
        .nargs(1)
        .required()
        .store_into(state.options.windowWidth);
    parser.add_argument("--height")
        .help("Smoke window height in pixels")
        .default_value(state.options.windowHeight)
        .nargs(1)
        .required()
        .store_into(state.options.windowHeight);
}

void addOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("-h", "--help").help("Print this help message").flag().store_into(state.options.showHelp);
    addProfileOptions(parser, state);
    addWindowOptions(parser, state);
    addTimingOptions(parser, state);
}

auto validateWindowExtent(const Options& options) -> std::expected<void, std::string>
{
    if (options.windowWidth == 0)
    {
        return std::unexpected {"--width must be greater than zero"};
    }
    if (options.windowHeight == 0)
    {
        return std::unexpected {"--height must be greater than zero"};
    }
    return {};
}

auto validateDurations(const ParsingState& state) -> std::expected<void, std::string>
{
    if (state.warmupSeconds < 0 || std::chrono::seconds {state.warmupSeconds} > maximumWarmupDuration)
    {
        return std::unexpected {"--warmup-seconds must be between 0 and 3600"};
    }
    if (state.measurementSeconds < 1 || std::chrono::seconds {state.measurementSeconds} > maximumMeasurementDuration)
    {
        return std::unexpected {"--measure-seconds must be between 1 and 86400"};
    }
    return {};
}

auto validateRunCount(const ParsingState& state) -> std::expected<void, std::string>
{
    if (state.runCount < 1 || state.runCount > maximumRunCount)
    {
        return std::unexpected {"--runs must be between 1 and 100"};
    }
    return {};
}

auto validateOptions(const ParsingState& state) -> std::expected<void, std::string>
{
    if (state.options.profile != "smoke")
    {
        return std::unexpected {"Only the smoke profile is implemented"};
    }
    if (const auto extent = validateWindowExtent(state.options); !extent)
    {
        return extent;
    }
    if (const auto durations = validateDurations(state); !durations)
    {
        return durations;
    }
    return validateRunCount(state);
}
}

auto parseArguments(int argumentCount, char** arguments) -> std::expected<Options, std::string>
{
    auto state = ParsingState {};
    auto parser = argparse::ArgumentParser {"panda-bench", "", argparse::default_arguments::none};
    addOptions(parser, state);
    try
    {
        parser.parse_args(argumentCount, arguments);
    }
    catch (const std::exception& error)
    {
        return std::unexpected {error.what()};
    }
    if (state.options.showHelp)
    {
        return state.options;
    }
    if (const auto validation = validateOptions(state); !validation)
    {
        return std::unexpected {validation.error()};
    }

    state.options.outputDirectory = state.outputDirectory;
    state.options.warmupDuration = std::chrono::seconds {state.warmupSeconds};
    state.options.measurementDuration = std::chrono::seconds {state.measurementSeconds};
    state.options.runCount = static_cast<std::size_t>(state.runCount);
    return state.options;
}

auto usage() -> std::string
{
    auto state = ParsingState {};
    auto parser = argparse::ArgumentParser {"panda-bench", "", argparse::default_arguments::none};
    addOptions(parser, state);
    return parser.help().str();
}
}

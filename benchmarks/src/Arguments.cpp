#include <algorithm>
#include <argparse/argparse.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <panda/EngineOptions.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda_bench/Arguments.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace panda_bench
{
namespace
{
constexpr auto maximumWarmupDuration = std::chrono::hours {1};
constexpr auto maximumMeasurementDuration = std::chrono::hours {24};
constexpr auto maximumRunCount = std::int64_t {100};
constexpr auto particleCounts = std::array {10'000U, 100'000U, 1'000'000U};
constexpr auto unspecifiedShadowedLightCount = std::numeric_limits<std::int64_t>::min();
constexpr auto textureFilteringPresets = std::array<std::pair<std::string_view, panda::TextureFilteringPreset>, 7> {
    {
     {"nearest", panda::TextureFilteringPreset::Nearest},
     {"bilinear", panda::TextureFilteringPreset::Bilinear},
     {"trilinear", panda::TextureFilteringPreset::Trilinear},
     {"anisotropic-2x", panda::TextureFilteringPreset::Anisotropic2x},
     {"anisotropic-4x", panda::TextureFilteringPreset::Anisotropic4x},
     {"anisotropic-8x", panda::TextureFilteringPreset::Anisotropic8x},
     {"anisotropic-16x", panda::TextureFilteringPreset::Anisotropic16x},
     }
};

struct ParsingState
{
    Options options;
    std::string outputDirectory {"results"};
    std::int64_t lightCount {defaultSmokeLightCount};
    std::int64_t shadowedLightCount {unspecifiedShadowedLightCount};
    std::int64_t particleCount {defaultParticleCount};
    std::int64_t warmupSeconds {1};
    std::int64_t measurementSeconds {3};
    std::int64_t runCount {1};
    std::string presentationMode {"fifo"};
    std::int64_t sampleCount {static_cast<std::int64_t>(panda::SampleCount::Four)};
    std::string textureFiltering {"anisotropic-8x"};
};

void addTextureFilteringOption(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--texture-filter")
        .help("Texture filtering preset: nearest, bilinear, trilinear, anisotropic-2x/4x/8x/16x")
        .default_value(std::string {"anisotropic-8x"})
        .nargs(1)
        .required()
        .store_into(state.textureFiltering);
}

void addProfileOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--profile")
        .help("Workload profile: smoke or gpu-particles")
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
    parser.add_argument("--present-mode")
        .help("Window presentation mode: fifo, mailbox or immediate")
        .default_value(std::string {"fifo"})
        .nargs(1)
        .required()
        .store_into(state.presentationMode);
    parser.add_argument("--samples")
        .help("Requested samples per pixel (1 disables MSAA; 1, 2 or 4)")
        .default_value(state.sampleCount)
        .nargs(1)
        .required()
        .store_into(state.sampleCount);
    addTextureFilteringOption(parser, state);
}

void addParticleCountOption(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--particle-count")
        .help("Particle count for gpu-particles: 10000, 100000 or 1000000")
        .default_value(std::int64_t {defaultParticleCount})
        .nargs(1)
        .required()
        .store_into(state.particleCount);
}

void addLightingOptions(argparse::ArgumentParser& parser, ParsingState& state)
{
    parser.add_argument("--lights")
        .help("Smoke profile: total lights (0 to 1000; one directional and the rest point lights)")
        .default_value(std::int64_t {defaultSmokeLightCount})
        .nargs(1)
        .required()
        .store_into(state.lightCount);
    parser.add_argument("--shadowed-lights")
        .help("Smoke profile: shadowed lights (0 to --lights; defaults to min(--lights, 65))")
        .default_value(unspecifiedShadowedLightCount)
        .nargs(1)
        .required()
        .store_into(state.shadowedLightCount);
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
    addParticleCountOption(parser, state);
    addLightingOptions(parser, state);
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

auto validateLightCount(const ParsingState& state) -> std::expected<void, std::string>
{
    if (state.lightCount < 0 || std::cmp_greater(state.lightCount, maximumSmokeLightCount))
    {
        return std::unexpected {"--lights must be between 0 and 1000"};
    }
    return {};
}

auto validateShadowedLightCount(ParsingState& state) -> std::expected<void, std::string>
{
    const auto shadowedLightCount = state.shadowedLightCount == unspecifiedShadowedLightCount
                                        ? std::min(state.lightCount, std::int64_t {defaultSmokeShadowedLightCount})
                                        : state.shadowedLightCount;
    if (shadowedLightCount < 0 || std::cmp_greater(shadowedLightCount, state.lightCount))
    {
        return std::unexpected {"--shadowed-lights must be between 0 and --lights"};
    }
    state.options.shadowedLightCount = static_cast<std::uint32_t>(shadowedLightCount);
    return {};
}

auto validateLighting(ParsingState& state) -> std::expected<void, std::string>
{
    if (const auto lights = validateLightCount(state); !lights)
    {
        return lights;
    }
    return validateShadowedLightCount(state);
}

auto parsePresentationMode(std::string_view mode) -> std::expected<panda::EngineOptions::PresentationMode, std::string>
{
    if (mode == "fifo")
    {
        return panda::EngineOptions::PresentationMode::Fifo;
    }
    if (mode == "mailbox")
    {
        return panda::EngineOptions::PresentationMode::Mailbox;
    }
    if (mode == "immediate")
    {
        return panda::EngineOptions::PresentationMode::Immediate;
    }
    return std::unexpected {"--present-mode must be fifo, mailbox or immediate"};
}

auto parseTextureFilteringPreset(std::string_view preset) -> std::expected<panda::TextureFilteringPreset, std::string>
{
    for (const auto& [name, value] : textureFilteringPresets)
    {
        if (preset == name)
        {
            return value;
        }
    }
    return std::unexpected {"--texture-filter must be nearest, bilinear, trilinear or anisotropic-2x/4x/8x/16x"};
}

auto validateProfileOptions(const ParsingState& state) -> std::expected<void, std::string>
{
    if (state.options.profile != "smoke" && state.options.profile != "gpu-particles")
    {
        return std::unexpected {"Profile must be smoke or gpu-particles"};
    }
    const auto isParticleProfile = state.options.profile == "gpu-particles";
    if (!isParticleProfile && std::cmp_not_equal(state.particleCount, defaultParticleCount))
    {
        return std::unexpected {"--particle-count is only valid with --profile gpu-particles"};
    }
    if (isParticleProfile && (std::cmp_not_equal(state.lightCount, defaultSmokeLightCount) ||
                              (state.shadowedLightCount != unspecifiedShadowedLightCount &&
                               std::cmp_not_equal(state.shadowedLightCount, defaultSmokeShadowedLightCount))))
    {
        return std::unexpected {"--lights and --shadowed-lights are only valid with --profile smoke"};
    }
    if (isParticleProfile && !std::ranges::any_of(particleCounts, [&state](std::uint32_t count) {
            return std::cmp_equal(state.particleCount, count);
        }))
    {
        return std::unexpected {"--particle-count must be 10000, 100000 or 1000000"};
    }
    return {};
}

auto validateProfileAndPresentationMode(ParsingState& state) -> std::expected<void, std::string>
{
    if (const auto profile = validateProfileOptions(state); !profile)
    {
        return profile;
    }
    const auto presentationMode = parsePresentationMode(state.presentationMode);
    if (!presentationMode)
    {
        return std::unexpected {presentationMode.error()};
    }
    state.options.presentationMode = *presentationMode;
    if (state.sampleCount != 1 && state.sampleCount != 2 && state.sampleCount != 4)
    {
        return std::unexpected {"Sample count must be 1, 2 or 4"};
    }
    state.options.sampleCount = static_cast<panda::SampleCount>(state.sampleCount);
    const auto textureFiltering = parseTextureFilteringPreset(state.textureFiltering);
    if (!textureFiltering)
    {
        return std::unexpected {textureFiltering.error()};
    }
    state.options.textureFilteringPreset = *textureFiltering;
    return {};
}

auto validateOptions(ParsingState& state) -> std::expected<void, std::string>
{
    if (const auto profile = validateProfileAndPresentationMode(state); !profile)
    {
        return profile;
    }
    if (const auto extent = validateWindowExtent(state.options); !extent)
    {
        return extent;
    }
    if (const auto durations = validateDurations(state); !durations)
    {
        return durations;
    }
    if (const auto lighting = validateLighting(state); !lighting)
    {
        return lighting;
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
    state.options.lightCount = static_cast<std::uint32_t>(state.lightCount);
    state.options.particleCount = static_cast<std::uint32_t>(state.particleCount);
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

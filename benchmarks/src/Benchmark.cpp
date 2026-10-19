#include "Benchmark.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <panda/HostInfo.hpp>
#include <panda/Logger.hpp>
#include <panda_bench/Arguments.hpp>
#include <panda_bench/Report.hpp>
#include <panda_bench/Statistics.hpp>
#include <print>
#include <string>
#include <tuple>
#include <utility>

#include "SmokeRun.hpp"

#ifndef PANDA_BENCH_REVISION
#    define PANDA_BENCH_REVISION "unknown"
#endif
#ifndef PANDA_BENCH_COMPILER
#    define PANDA_BENCH_COMPILER "unknown"
#endif
#ifndef PANDA_BENCH_OS
#    define PANDA_BENCH_OS "unknown"
#endif
#ifndef PANDA_BENCH_BUILD_MODE
#    define PANDA_BENCH_BUILD_MODE "unknown"
#endif
namespace panda_bench
{
namespace
{
auto makeReport(const Options& options) -> Report
{
    const auto hostInfo = panda::getHostInfo();
    auto report = Report {};
    report.revision = PANDA_BENCH_REVISION;
    report.operatingSystem = PANDA_BENCH_OS;
    report.compiler = PANDA_BENCH_COMPILER;
    report.buildMode = PANDA_BENCH_BUILD_MODE;
    report.cpuModel = hostInfo.cpuModel;
    report.requestedWindowWidth = options.windowWidth;
    report.requestedWindowHeight = options.windowHeight;
    report.requestedWarmup = options.warmupDuration;
    report.requestedMeasurement = options.measurementDuration;
    report.requestedRuns = options.runCount;
    return report;
}

auto executeRuns(const Options& options, Report& report) -> bool
{
    for (auto index = std::size_t {}; index < options.runCount; ++index)
    {
        auto run = RunReport {};
        run.runIndex = static_cast<std::uint32_t>(index + 1);
        const auto execution = SmokeRun {options, run, report}.execute();
        report.runs.push_back(std::move(run));
        if (!execution)
        {
            report.outcome = execution.error().outcome;
            report.outcomeMessage = execution.error().message;
            std::println(stderr, "{}", execution.error().message);
            return false;
        }
    }
    return true;
}

auto prepareOutputDirectory(const std::filesystem::path& directory) -> std::expected<void, std::string>
{
    try
    {
        std::filesystem::create_directories(directory);
        return {};
    }
    catch (const std::exception& error)
    {
        return std::unexpected {error.what()};
    }
}

auto addLogFileSink(const std::filesystem::path& directory) -> std::expected<std::filesystem::path, std::string>
{
    auto logPath = directory / "panda-bench.log";
    auto sink = panda::log::FileSink::open(logPath);
    if (!sink)
    {
        return std::unexpected {sink.error()};
    }
    std::ignore = panda::log::Logger::instance().addSink(std::move(*sink));
    return logPath;
}

auto writeReport(const Options& options, const Report& report) -> std::expected<std::filesystem::path, std::string>
{
    try
    {
        auto outputPath = options.outputDirectory / "panda-bench-smoke.json";
        auto output = std::ofstream {outputPath, std::ios::binary};
        if (!output)
        {
            return std::unexpected {"Could not open benchmark report: " + outputPath.string()};
        }
        output << serialize(report);
        if (!output)
        {
            return std::unexpected {"Could not write benchmark report: " + outputPath.string()};
        }
        return outputPath;
    }
    catch (const std::exception& error)
    {
        return std::unexpected {error.what()};
    }
}

void printSummary(const Report& report, const std::filesystem::path& outputPath, const std::filesystem::path& logPath)
{
    for (const auto& run : report.runs)
    {
        const auto cpuStatistics = summarizeCpuFrameDuration(run.frameSamples);
        const auto gpuStatistics = summarizeGpuExecutionDuration(run.frameSamples);
        std::print("Run {}: startup {} ns, teardown {} ns, frame samples {}",
                   run.runIndex,
                   run.startupDuration.count(),
                   run.teardownDuration.count(),
                   run.frameSamples.size());
        if (cpuStatistics)
        {
            std::print(", CPU frame p50 {} ns, p95 {} ns", cpuStatistics->p50.count(), cpuStatistics->p95.count());
        }
        if (gpuStatistics)
        {
            std::print(", GPU p50 {} ns, p95 {} ns", gpuStatistics->p50.count(), gpuStatistics->p95.count());
        }
        std::println();
    }
    std::println("Report: {}", outputPath.string());
    std::println("Logs: {}", logPath.string());
}
}

auto runBenchmark(const Options& options) -> int
{
    auto report = makeReport(options);
    const auto outputDirectory = prepareOutputDirectory(options.outputDirectory);
    if (!outputDirectory)
    {
        std::println(stderr, "{}", outputDirectory.error());
        return EXIT_FAILURE;
    }
    const auto logPath = addLogFileSink(options.outputDirectory);
    if (!logPath)
    {
        std::println(stderr, "{}", logPath.error());
        return EXIT_FAILURE;
    }
    const auto runsSucceeded = executeRuns(options, report);
    const auto outputPath = writeReport(options, report);
    if (!outputPath)
    {
        std::println(stderr, "{}", outputPath.error());
        return EXIT_FAILURE;
    }
    printSummary(report, *outputPath, *logPath);
    return runsSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
}

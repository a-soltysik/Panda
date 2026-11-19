#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <panda/EngineOptions.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda_bench/Report.hpp>
#include <panda_bench/Statistics.hpp>
#include <string>
#include <utility>

namespace panda_bench
{
namespace
{
using Json = nlohmann::json;
constexpr auto benchmarkReportSchemaVersion = 9U;
constexpr auto smokeProfileVersion = 7U;

auto presentationModeName(panda::EngineOptions::PresentationMode mode) -> std::string
{
    switch (mode)
    {
    case panda::EngineOptions::PresentationMode::Fifo:
        return "fifo";
    case panda::EngineOptions::PresentationMode::Mailbox:
        return "mailbox";
    case panda::EngineOptions::PresentationMode::Immediate:
        return "immediate";
    }
    return "unknown";
}

auto textureFilteringPresetName(panda::TextureFilteringPreset preset) -> std::string
{
    switch (preset)
    {
    case panda::TextureFilteringPreset::Nearest:
        return "nearest";
    case panda::TextureFilteringPreset::Bilinear:
        return "bilinear";
    case panda::TextureFilteringPreset::Trilinear:
        return "trilinear";
    case panda::TextureFilteringPreset::Anisotropic2x:
        return "anisotropic-2x";
    case panda::TextureFilteringPreset::Anisotropic4x:
        return "anisotropic-4x";
    case panda::TextureFilteringPreset::Anisotropic8x:
        return "anisotropic-8x";
    case panda::TextureFilteringPreset::Anisotropic16x:
        return "anisotropic-16x";
    }
    return "unknown";
}

auto statisticsJson(const std::optional<SampleStatistics>& statistics) -> Json
{
    if (!statistics)
    {
        return nullptr;
    }
    return Json {
        {"sampleCount",        statistics->sampleCount    },
        {"meanNanoseconds",    statistics->meanNanoseconds},
        {"p50Nanoseconds",     statistics->p50.count()    },
        {"p95Nanoseconds",     statistics->p95.count()    },
        {"p99Nanoseconds",     statistics->p99.count()    },
        {"minimumNanoseconds", statistics->minimum.count()},
        {"maximumNanoseconds", statistics->maximum.count()}
    };
}

auto runJson(const RunReport& run) -> Json
{
    const auto cpuStatistics = summarizeCpuFrameDuration(run.frameSamples);
    const auto gpuStatistics = summarizeGpuExecutionDuration(run.frameSamples);
    auto rawSamples = Json::array();
    std::ranges::transform(run.frameSamples, std::back_inserter(rawSamples), [](const auto& sample) {
        return Json {
            {"frameIndex",                      sample.frameIndex                  },
            {"cpuFrameDurationNanoseconds",     sample.cpuFrameDuration.count()    },
            {"gpuExecutionDurationNanoseconds", sample.gpuExecutionDuration.count()}
        };
    });
    return Json {
        {"runIndex",                           run.runIndex                                                             },
        {"effectiveFramebufferExtent",         Json {{"width", run.framebufferWidth}, {"height", run.framebufferHeight}}},
        {"effectiveSampleCount",               run.effectiveSampleCount                                                 },
        {"effectiveAnisotropy",                run.effectiveAnisotropy                                                  },
        {"effectivePresentationMode",
         run.effectivePresentationMode ? Json(presentationModeName(*run.effectivePresentationMode)) : Json {}           },
        {"startupDurationNanoseconds",         run.startupDuration.count()                                              },
        {"warmupDurationNanoseconds",          run.warmupDuration.count()                                               },
        {"measurementWallDurationNanoseconds", run.measurementWallDuration.count()                                      },
        {"completionWaitDurationNanoseconds",  run.completionWaitDuration.count()                                       },
        {"sampleDrainDurationNanoseconds",     run.sampleDrainDuration.count()                                          },
        {"teardownDurationNanoseconds",        run.teardownDuration.count()                                             },
        {"warmupSubmittedFrames",              run.warmupSubmittedFrames                                                },
        {"measurementSubmittedFrames",         run.measurementSubmittedFrames                                           },
        {"cpuFrameDurationStatistics",         statisticsJson(cpuStatistics)                                            },
        {"gpuExecutionDurationStatistics",     statisticsJson(gpuStatistics)                                            },
        {"frameSamples",                       std::move(rawSamples)                                                    }
    };
}

auto settingsJson(const Report& report) -> Json
{
    return Json {
        {"requestedWindowExtent",
         Json {{"width", report.requestedWindowWidth}, {"height", report.requestedWindowHeight}}              },
        {"requestedLightCount",             report.requestedLightCount                                        },
        {"requestedShadowedLightCount",     report.requestedShadowedLightCount                                },
        {"requestedPresentationMode",       presentationModeName(report.requestedPresentationMode)            },
        {"requestedSampleCount",            static_cast<std::uint32_t>(report.requestedSampleCount)           },
        {"requestedTextureFilteringPreset", textureFilteringPresetName(report.requestedTextureFilteringPreset)},
        {"validationEnabled",               false                                                             },
        {"externalProfilerEnabled",         false                                                             },
        {"framePerformanceSamplingEnabled", true                                                              },
        {"requestedWarmupNanoseconds",
         std::chrono::duration_cast<std::chrono::nanoseconds>(report.requestedWarmup).count()                 },
        {"requestedMeasurementNanoseconds",
         std::chrono::duration_cast<std::chrono::nanoseconds>(report.requestedMeasurement).count()            },
        {"requestedRuns",                   report.requestedRuns                                              }
    };
}

auto environmentJson(const Report& report) -> Json
{
    auto environment = Json {
        {"operatingSystem", report.operatingSystem},
        {"compiler",        report.compiler       },
        {"buildMode",       report.buildMode      }
    };
    if (report.cpuModel && !report.cpuModel->empty())
    {
        environment.emplace("cpuModel", *report.cpuModel);
    }
    if (report.gpuModel && !report.gpuModel->empty())
    {
        environment.emplace("gpuModel", *report.gpuModel);
    }
    if (report.driver)
    {
        environment.emplace("driver",
                            Json {
                                {"name",    report.driver->name   },
                                {"info",    report.driver->info   },
                                {"version", report.driver->version}
        });
    }
    if (report.vulkanApiVersion)
    {
        environment.emplace("vulkanApiVersion", *report.vulkanApiVersion);
    }
    return environment;
}
}

auto serialize(const Report& report) -> std::string
{
    auto runs = Json::array();
    std::ranges::transform(report.runs, std::back_inserter(runs), runJson);
    return Json {
               {"schemaVersion", benchmarkReportSchemaVersion                                         },
               {"profile",       Json {{"name", "smoke"}, {"version", smokeProfileVersion}}           },
               {"workload",      "renderStaticColoredScene"                                           },
               {"code",          Json {{"revision", report.revision}}                                 },
               {"environment",   environmentJson(report)                                              },
               {"settings",      settingsJson(report)                                                 },
               {"runs",          std::move(runs)                                                      },
               {"outcome",       Json {{"status", report.outcome}, {"message", report.outcomeMessage}}}
    }
               .dump(2) +
           "\n";
}

auto writeReport(const std::filesystem::path& outputPath, const Report& report)
    -> std::expected<std::filesystem::path, std::string>
{
    try
    {
        auto output = std::ofstream {outputPath, std::ios::binary};
        if (!output)
        {
            return std::unexpected {"Could not open benchmark report: " + outputPath.string()};
        }
        output << serialize(report);
        output.close();
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
}

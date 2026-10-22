#include <gtest/gtest.h>

#include <chrono>
#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>
#include <panda_bench/Report.hpp>
#include <utility>

TEST(BenchmarkReport, SerializesSchemaMetadataAndPerFrameCpuAndGpuStatistics)
{
    auto report = panda_bench::Report {};
    report.revision = "123456789abc";
    report.operatingSystem = "Test OS";
    report.compiler = "Test compiler";
    report.buildMode = "Debug";
    report.cpuModel = "Test CPU";
    report.gpuModel = "Test GPU";
    report.driver = panda_bench::DriverReport {.name = "Test driver", .info = "Version 1.2.3", .version = 123};
    report.vulkanApiVersion = 0x00403000;
    report.requestedWindowWidth = 1920;
    report.requestedWindowHeight = 1080;
    report.requestedWarmup = std::chrono::seconds {2};
    report.requestedMeasurement = std::chrono::seconds {4};
    report.requestedRuns = 1;
    auto run = panda_bench::RunReport {};
    run.runIndex = 1;
    run.framebufferWidth = 1920;
    run.framebufferHeight = 1080;
    run.startupDuration = std::chrono::nanoseconds {10};
    run.completionWaitDuration = std::chrono::nanoseconds {20};
    run.sampleDrainDuration = std::chrono::nanoseconds {30};
    run.teardownDuration = std::chrono::nanoseconds {40};
    run.measurementSubmittedFrames = 2;
    run.frameSamples = {
        {.frameIndex = 7,
         .cpuFrameDuration = std::chrono::nanoseconds {150},
         .gpuExecutionDuration = std::chrono::nanoseconds {100}},
        {.frameIndex = 8,
         .cpuFrameDuration = std::chrono::nanoseconds {250},
         .gpuExecutionDuration = std::chrono::nanoseconds {200}}
    };
    report.runs.push_back(std::move(run));

    const auto json = nlohmann::json::parse(panda_bench::serialize(report));

    EXPECT_EQ(json.at("schemaVersion"), 3);
    EXPECT_EQ(json.at("profile").at("name"), "smoke");
    EXPECT_EQ(json.at("workload"), "presentClearFrame");
    EXPECT_EQ(json.at("code").at("revision"), "123456789abc");
    EXPECT_FALSE(json.at("code").contains("dirtyAtConfigure"));
    EXPECT_EQ(json.at("environment").at("cpuModel"), "Test CPU");
    EXPECT_EQ(json.at("environment").at("gpuModel"), "Test GPU");
    EXPECT_EQ(json.at("environment").at("driver").at("name"), "Test driver");
    EXPECT_EQ(json.at("environment").at("driver").at("info"), "Version 1.2.3");
    EXPECT_EQ(json.at("environment").at("driver").at("version"), 123);
    EXPECT_EQ(json.at("settings").at("requestedWindowExtent").at("width"), 1920);
    EXPECT_EQ(json.at("settings").at("requestedWindowExtent").at("height"), 1080);
    EXPECT_EQ(json.at("settings").at("requestedMeasurementNanoseconds"), 4'000'000'000LL);
    EXPECT_EQ(json.at("runs").size(), 1U);
    EXPECT_EQ(json.at("runs").at(0).at("effectiveFramebufferExtent").at("width"), 1920);
    EXPECT_EQ(json.at("runs").at(0).at("completionWaitDurationNanoseconds"), 20);
    EXPECT_EQ(json.at("runs").at(0).at("sampleDrainDurationNanoseconds"), 30);
    EXPECT_EQ(json.at("runs").at(0).at("cpuFrameDurationStatistics").at("p50Nanoseconds"), 150);
    EXPECT_EQ(json.at("runs").at(0).at("cpuFrameDurationStatistics").at("p95Nanoseconds"), 250);
    EXPECT_EQ(json.at("runs").at(0).at("gpuExecutionDurationStatistics").at("p50Nanoseconds"), 100);
    EXPECT_EQ(json.at("runs").at(0).at("gpuExecutionDurationStatistics").at("p95Nanoseconds"), 200);
    EXPECT_EQ(json.at("runs").at(0).at("frameSamples").size(), 2U);
    EXPECT_EQ(json.at("runs").at(0).at("frameSamples").at(0).at("cpuFrameDurationNanoseconds"), 150);
    EXPECT_EQ(json.at("runs").at(0).at("frameSamples").at(0).at("gpuExecutionDurationNanoseconds"), 100);
    EXPECT_EQ(json.at("outcome").at("status"), "complete");
}

TEST(BenchmarkReport, OmitsEnvironmentValuesThatWereNotCollected)
{
    const auto json = nlohmann::json::parse(panda_bench::serialize(panda_bench::Report {}));
    const auto& environment = json.at("environment");

    EXPECT_FALSE(environment.contains("cpuModel"));
    EXPECT_FALSE(environment.contains("gpuModel"));
    EXPECT_FALSE(environment.contains("driver"));
    EXPECT_FALSE(environment.contains("vulkanApiVersion"));
}

TEST(BenchmarkReport, ReportsBufferedWriteFailureWhenClosingOutput)
{
#ifdef __linux__
    const auto result = panda_bench::writeReport("/dev/full", panda_bench::Report {});

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("Could not write benchmark report"), std::string::npos);
#else
    GTEST_SKIP() << "The buffered write failure device is Linux-specific";
#endif
}

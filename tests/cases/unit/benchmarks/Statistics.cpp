#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <panda/PerformanceManager.hpp>
#include <panda_bench/Statistics.hpp>
#include <vector>

namespace
{
auto sample(std::uint64_t index, std::chrono::nanoseconds cpuDuration, std::chrono::nanoseconds gpuDuration)
    -> panda::FramePerformanceSample
{
    return {.frameIndex = index, .cpuFrameDuration = cpuDuration, .gpuExecutionDuration = gpuDuration};
}
}

TEST(BenchmarkStatistics, EmptyInputHasNoStatistics)
{
    const auto cpuResult = panda_bench::summarizeCpuFrameDuration({});
    const auto gpuResult = panda_bench::summarizeGpuExecutionDuration({});

    EXPECT_FALSE(cpuResult);
    EXPECT_FALSE(gpuResult);
}

TEST(BenchmarkStatistics, ComputesNearestRankStatisticsFromUnorderedSamples)
{
    auto samples = std::vector<panda::FramePerformanceSample> {};
    for (auto value = std::int64_t {100}; value > 0; --value)
    {
        samples.push_back(sample(static_cast<std::uint64_t>(value),
                                 std::chrono::nanoseconds {value * 2},
                                 std::chrono::nanoseconds {value}));
    }

    const auto result = panda_bench::summarizeGpuExecutionDuration(samples);

    ASSERT_TRUE(result.has_value()) << "Expected statistics for nonempty samples";
    const auto& statistics = result.value();
    EXPECT_EQ(statistics.sampleCount, 100U);
    EXPECT_DOUBLE_EQ(statistics.meanNanoseconds, 50.5);
    EXPECT_EQ(statistics.p50, std::chrono::nanoseconds {50});
    EXPECT_EQ(statistics.p95, std::chrono::nanoseconds {95});
    EXPECT_EQ(statistics.p99, std::chrono::nanoseconds {99});
    EXPECT_EQ(statistics.minimum, std::chrono::nanoseconds {1});
    EXPECT_EQ(statistics.maximum, std::chrono::nanoseconds {100});

    const auto cpuResult = panda_bench::summarizeCpuFrameDuration(samples);
    ASSERT_TRUE(cpuResult.has_value()) << "Expected CPU statistics for nonempty samples";
    const auto& cpuStatistics = cpuResult.value();
    EXPECT_DOUBLE_EQ(cpuStatistics.meanNanoseconds, 101.0);
    EXPECT_EQ(cpuStatistics.p50, std::chrono::nanoseconds {100});
    EXPECT_EQ(cpuStatistics.p95, std::chrono::nanoseconds {190});
    EXPECT_EQ(cpuStatistics.p99, std::chrono::nanoseconds {198});
    EXPECT_EQ(cpuStatistics.minimum, std::chrono::nanoseconds {2});
    EXPECT_EQ(cpuStatistics.maximum, std::chrono::nanoseconds {200});
}

TEST(BenchmarkStatistics, SingleSampleUsesThatSampleForEveryQuantile)
{
    const auto samples = std::vector {sample(4, std::chrono::nanoseconds {1624}, std::chrono::nanoseconds {812})};

    const auto result = panda_bench::summarizeGpuExecutionDuration(samples);

    ASSERT_TRUE(result.has_value()) << "Expected statistics for one sample";
    const auto& statistics = result.value();
    EXPECT_EQ(statistics.sampleCount, 1U);
    EXPECT_DOUBLE_EQ(statistics.meanNanoseconds, 812.0);
    EXPECT_EQ(statistics.p50, std::chrono::nanoseconds {812});
    EXPECT_EQ(statistics.p95, std::chrono::nanoseconds {812});
    EXPECT_EQ(statistics.p99, std::chrono::nanoseconds {812});
}

TEST(BenchmarkStatistics, MeanRetainsPrecisionWhenTheNanosecondSumExceedsDoubleExactRange)
{
    constexpr auto largeDuration = std::chrono::nanoseconds {9'007'199'254'740'993};
    const auto samples = std::vector {sample(0, std::chrono::nanoseconds {0}, largeDuration),
                                      sample(1, std::chrono::nanoseconds {0}, largeDuration),
                                      sample(2, std::chrono::nanoseconds {0}, largeDuration),
                                      sample(3, std::chrono::nanoseconds {0}, std::chrono::nanoseconds {1})};

    const auto result = panda_bench::summarizeGpuExecutionDuration(samples);

    ASSERT_TRUE(result.has_value()) << "Expected statistics for high-duration samples";
    const auto& statistics = result.value();
    EXPECT_DOUBLE_EQ(statistics.meanNanoseconds, 6'755'399'441'055'745.0);
}

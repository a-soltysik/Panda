#include <gtest/gtest.h>

#include <algorithm>
#include <expected>
#include <iterator>
#include <panda_bench/Arguments.hpp>
#include <string>
#include <vector>

namespace
{
auto parse(const std::vector<std::string>& arguments) -> std::expected<panda_bench::Options, std::string>
{
    auto mutableArguments = arguments;
    auto pointers = std::vector<char*> {};
    pointers.reserve(mutableArguments.size());
    std::ranges::transform(mutableArguments, std::back_inserter(pointers), [](auto& argument) {
        return argument.data();
    });
    return panda_bench::parseArguments(static_cast<int>(pointers.size()), pointers.data());
}
}

TEST(BenchmarkArguments, SuppliesTheSmokeDefaults)
{
    const auto options = parse({"panda-bench"});

    ASSERT_TRUE(options);
    EXPECT_EQ(options->profile, "smoke");
    EXPECT_EQ(options->outputDirectory, "results");
    EXPECT_EQ(options->windowWidth, 1280U);
    EXPECT_EQ(options->windowHeight, 720U);
    EXPECT_EQ(options->warmupDuration, std::chrono::seconds {1});
    EXPECT_EQ(options->measurementDuration, std::chrono::seconds {3});
    EXPECT_EQ(options->runCount, 1U);
    EXPECT_FALSE(options->showHelp);
}

TEST(BenchmarkArguments, ReadsRuntimeSettings)
{
    const auto options = parse({"panda-bench",
                                "--profile",
                                "smoke",
                                "--output",
                                "captures/first-run",
                                "--width",
                                "1920",
                                "--height",
                                "1080",
                                "--warmup-seconds",
                                "0",
                                "--measure-seconds",
                                "12",
                                "--runs",
                                "3"});

    ASSERT_TRUE(options);
    EXPECT_EQ(options->outputDirectory, "captures/first-run");
    EXPECT_EQ(options->windowWidth, 1920U);
    EXPECT_EQ(options->windowHeight, 1080U);
    EXPECT_EQ(options->warmupDuration, std::chrono::seconds {0});
    EXPECT_EQ(options->measurementDuration, std::chrono::seconds {12});
    EXPECT_EQ(options->runCount, 3U);
}

TEST(BenchmarkArguments, HandlesHelp)
{
    const auto options = parse({"panda-bench", "--help"});

    ASSERT_TRUE(options);
    EXPECT_TRUE(options->showHelp);
}

TEST(BenchmarkArguments, GeneratesHelpForRegisteredOptions)
{
    const auto help = panda_bench::usage();

    EXPECT_NE(help.find("--profile"), std::string::npos);
    EXPECT_NE(help.find("--output"), std::string::npos);
    EXPECT_NE(help.find("--width"), std::string::npos);
    EXPECT_NE(help.find("--height"), std::string::npos);
    EXPECT_NE(help.find("--warmup-seconds"), std::string::npos);
    EXPECT_NE(help.find("--measure-seconds"), std::string::npos);
    EXPECT_NE(help.find("--runs"), std::string::npos);
}

TEST(BenchmarkArguments, RejectsUnsupportedProfile)
{
    const auto options = parse({"panda-bench", "--profile", "reference-1080p"});

    ASSERT_FALSE(options);
    EXPECT_NE(options.error().find("Only the smoke profile is implemented"), std::string::npos);
}

TEST(BenchmarkArguments, RejectsMissingAndUnknownValues)
{
    EXPECT_FALSE(parse({"panda-bench", "--profile"}));
    EXPECT_FALSE(parse({"panda-bench", "--output"}));
    EXPECT_FALSE(parse({"panda-bench", "--width"}));
    EXPECT_FALSE(parse({"panda-bench", "--height"}));
    EXPECT_FALSE(parse({"panda-bench", "--warmup-seconds"}));
    EXPECT_FALSE(parse({"panda-bench", "--measure-seconds"}));
    EXPECT_FALSE(parse({"panda-bench", "--runs"}));
    EXPECT_FALSE(parse({"panda-bench", "--not-an-option"}));
    EXPECT_FALSE(parse({"panda-bench", "--measure-seconds", "zero"}));
}

TEST(BenchmarkArguments, EnforcesControlledRunBounds)
{
    EXPECT_FALSE(parse({"panda-bench", "--warmup-seconds", "3601"}));
    EXPECT_FALSE(parse({"panda-bench", "--warmup-seconds", "-1"}));
    EXPECT_FALSE(parse({"panda-bench", "--measure-seconds", "0"}));
    EXPECT_FALSE(parse({"panda-bench", "--runs", "101"}));
    EXPECT_FALSE(parse({"panda-bench", "--width", "0"}));
    EXPECT_FALSE(parse({"panda-bench", "--width", "-1"}));
    EXPECT_FALSE(parse({"panda-bench", "--height", "4294967296"}));
}

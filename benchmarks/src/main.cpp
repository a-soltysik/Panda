#include <cstdio>
#include <cstdlib>
#include <exception>
#include <panda_bench/Arguments.hpp>
#include <print>
#include <string_view>

#include "Benchmark.hpp"

namespace
{
auto printError(std::FILE* stream, std::string_view message) noexcept -> int
{
    try
    {
        std::println(stream, "{}", message);
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
}

auto main(int argumentCount, char** arguments) -> int
{
    try
    {
        const auto options = panda_bench::parseArguments(argumentCount, arguments);
        if (!options)
        {
            std::println(stderr, "{}\nUse --help for available options.", options.error());
            return EXIT_FAILURE;
        }
        if (options->showHelp)
        {
            std::print("{}", panda_bench::usage());
            return EXIT_SUCCESS;
        }
        return panda_bench::runBenchmark(*options);
    }
    catch (const std::exception& error)
    {
        return printError(stderr, error.what());
    }
    catch (...)
    {
        return printError(stderr, "Unexpected benchmark failure");
    }
}

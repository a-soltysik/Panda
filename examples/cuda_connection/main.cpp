#include <cstdio>
#include <cstdlib>
#include <exception>
#include <panda/Panda.hpp>
#include <panda/cuda/Cuda.hpp>
#include <print>
#include <tuple>

#include "ProducerBoundary.hpp"

auto main() -> int
{
    try
    {
        if (panda::version().empty())
        {
            return EXIT_FAILURE;
        }

        const auto availability = panda::cuda::queryAvailability();
        if (!availability)
        {
            std::println(stderr, "CUDA initialization failed (native status {})", availability.error().nativeCode);
            return EXIT_FAILURE;
        }
        if (*availability == panda::cuda::Availability::NoDriver)
        {
            std::println("CUDA driver unavailable; Panda core remains usable");
            return EXIT_SUCCESS;
        }
        if (*availability == panda::cuda::Availability::NoDevice)
        {
            std::println("CUDA device unavailable; Panda core remains usable");
            return EXIT_SUCCESS;
        }

        const auto result = runProducer();
        if (result.nativeCode != 0 || result.value != producedValue)
        {
            std::println(stderr, "CUDA producer failed (native status {}, value {})", result.nativeCode, result.value);
            return EXIT_FAILURE;
        }
        std::println("CUDA producer returned {}", result.value);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::ignore = std::fputs("Unexpected application failure: ", stderr);
        std::ignore = std::fputs(error.what(), stderr);
        std::ignore = std::fputc('\n', stderr);
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::ignore = std::fputs("Unexpected application failure\n", stderr);
        return EXIT_FAILURE;
    }
}

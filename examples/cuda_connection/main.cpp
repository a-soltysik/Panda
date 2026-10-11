#include <cstdio>
#include <cstdlib>
#include <exception>
#include <panda/Assert.hpp>
#include <panda/Logger.hpp>
#include <panda/cuda/Cuda.hpp>
#include <print>
#include <string_view>

#include "ProducerBoundary.hpp"

namespace
{
auto fail(std::string_view message) noexcept -> int
{
    try
    {
        panda::log::error("{}", message);
    }
    catch (...)
    {
        panda::panic(message);
    }
    return EXIT_FAILURE;
}

auto runKernel() -> int
{
    const auto result = runProducer();
    if (result.nativeCode != 0 || result.value != producedValue)
    {
        std::println(stderr, "CUDA producer failed (native status {}, value {})", result.nativeCode, result.value);
        return EXIT_FAILURE;
    }
    std::println("CUDA producer returned {}", result.value);
    return EXIT_SUCCESS;
}
}

auto main() -> int
{
    try
    {
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

        return runKernel();
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }
    catch (...)
    {
        return fail("Unexpected application failure");
    }
}

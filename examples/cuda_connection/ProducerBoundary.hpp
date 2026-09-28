#pragma once

#include <cstdint>

// Application-owned C++20/CUDA20 boundary. No Panda or C++23 headers cross it.
static constexpr std::uint32_t producedValue = 42;

struct ProducerResult
{
    std::int32_t nativeCode;
    std::uint32_t value;
};

[[nodiscard]] auto runProducer() noexcept -> ProducerResult;
[[nodiscard]] auto launchProducerKernel(std::uint32_t* output) noexcept -> std::int32_t;

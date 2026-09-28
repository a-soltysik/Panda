#include <cuda_runtime.h>

#include <cstdint>

#include "ProducerBoundary.hpp"

namespace
{

__global__ void produceValue(std::uint32_t* output)
{
    *output = producedValue;
}

}

auto launchProducerKernel(std::uint32_t* output) noexcept -> std::int32_t
{
    produceValue<<<1, 1>>>(output);
    return static_cast<std::int32_t>(cudaGetLastError());
}

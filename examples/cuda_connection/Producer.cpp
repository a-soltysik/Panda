#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <cstdint>

#include "ProducerBoundary.hpp"

auto runProducer() noexcept -> ProducerResult
{
    void* nativeMemory = nullptr;
    const auto allocationStatus = cudaMalloc(&nativeMemory, sizeof(std::uint32_t));
    if (allocationStatus != cudaSuccess)
    {
        return {.nativeCode = static_cast<std::int32_t>(allocationStatus), .value = 0};
    }

    auto* const deviceMemory = static_cast<std::uint32_t*>(nativeMemory);
    const auto launchStatus = launchProducerKernel(deviceMemory);
    auto value = std::uint32_t {};
    const auto copyStatus =
        launchStatus == 0 ? cudaMemcpy(&value, deviceMemory, sizeof(value), cudaMemcpyDeviceToHost) : cudaSuccess;
    const auto freeStatus = cudaFree(deviceMemory);
    if (launchStatus != 0)
    {
        return {.nativeCode = launchStatus, .value = 0};
    }
    if (copyStatus != cudaSuccess)
    {
        return {.nativeCode = static_cast<std::int32_t>(copyStatus), .value = 0};
    }
    if (freeStatus != cudaSuccess)
    {
        return {.nativeCode = static_cast<std::int32_t>(freeStatus), .value = 0};
    }
    return {.nativeCode = 0, .value = value};
}

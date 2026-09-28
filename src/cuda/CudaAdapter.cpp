#include "CudaAdapter.hpp"

#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <cstdint>

namespace panda::cuda::detail
{

auto queryNativeAvailability() noexcept -> NativeAvailabilityResult
{
    auto deviceCount = int {};
    const auto status = cudaGetDeviceCount(&deviceCount);
    if (status == cudaErrorNoDevice)
    {
        return {.availability = NativeAvailability::NoDevice, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (status == cudaErrorInsufficientDriver || status == cudaErrorSystemDriverMismatch)
    {
        return {.availability = NativeAvailability::NoDriver, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (status != cudaSuccess)
    {
        return {.availability = NativeAvailability::Failure, .nativeCode = static_cast<std::int32_t>(status)};
    }
    if (deviceCount == 0)
    {
        return {.availability = NativeAvailability::NoDevice, .nativeCode = 0};
    }
    return {.availability = NativeAvailability::Available, .nativeCode = 0};
}

}

#pragma once

/// @file
/// Optional CUDA availability and external-buffer interoperability API.

#include <cstdint>
#include <panda/Error.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>

namespace panda::cuda
{
/// @brief CUDA availability on this machine at the time of the query.
enum class Availability : std::uint8_t
{
    Available,
    NoDriver,
    NoDevice
};

/// @brief Checks whether CUDA Runtime can see a usable device.
/// This may initialize the CUDA Runtime and block; it does not create a Panda GPU
/// session or choose a device for Vulkan interoperability.
/// @return Availability for ordinary absence, or a Panda error with the native
/// CUDA status for another initialization failure. A successful result does not
/// guarantee later launches.
[[nodiscard]] auto queryAvailability() -> Result<Availability>;
}

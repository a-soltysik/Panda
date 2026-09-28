#pragma once

#include <cstdint>
#include <expected>

namespace panda::cuda
{

/// @brief CUDA availability on this machine at the time of the query.
enum class Availability : std::uint8_t
{
    Available,
    NoDriver,
    NoDevice
};

/// @brief Unexpected CUDA Runtime initialization failure.
struct Error
{
    /// Native cudaError_t value for diagnostic reporting.
    std::int32_t nativeCode;
};

/// @brief Checks whether CUDA Runtime can see a usable device.
/// This may initialize the CUDA Runtime and block; it does not create a Panda GPU
/// session or choose a device for Vulkan interoperability.
/// @return Availability for ordinary absence, or the native status of another
/// initialization failure. A successful result does not guarantee later launches.
[[nodiscard]] auto queryAvailability() noexcept -> std::expected<Availability, Error>;

}

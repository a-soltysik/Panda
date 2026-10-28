#include <expected>
#include <panda/cuda/Cuda.hpp>

#include "CudaAdapter.hpp"

namespace panda::cuda
{
auto queryAvailability() noexcept -> std::expected<Availability, Error>
{
    const auto result = detail::queryNativeAvailability();
    switch (result.availability)
    {
    case detail::NativeAvailability::Available:
        return Availability::Available;
    case detail::NativeAvailability::NoDriver:
        return Availability::NoDriver;
    case detail::NativeAvailability::NoDevice:
        return Availability::NoDevice;
    case detail::NativeAvailability::Failure:
        return std::unexpected {Error {.nativeCode = result.nativeCode}};
    }
    return std::unexpected {Error {.nativeCode = result.nativeCode}};
}
}

#include <expected>
#include <panda/Error.hpp>
#include <panda/cuda/Cuda.hpp>

#include "CudaAdapter.hpp"

namespace panda::cuda
{
auto queryAvailability() -> Result<Availability>
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
        break;
    }
    return std::unexpected {makeError(ErrorCode::BackendFailure,
                                      "CUDA Runtime availability query failed",
                                      NativeError {.api = "CUDA", .code = result.nativeCode})};
}
}

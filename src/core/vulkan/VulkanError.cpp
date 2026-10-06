#include "VulkanError.hpp"

#include <cstdint>
#include <panda/Error.hpp>
#include <source_location>
#include <string>
#include <utility>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>

namespace panda
{
namespace
{
auto classify(vk::Result result) -> ErrorCode
{
    // Keep this switch exhaustive: -Wswitch-enum requires a deliberate choice
    // when the Vulkan SDK adds a distinct result value. Unknown runtime values
    // fall through to BackendFailure below.
    switch (result)
    {
    case vk::Result::eTimeout:
        return ErrorCode::Timeout;
    case vk::Result::eErrorFeatureNotPresent:
    case vk::Result::eErrorExtensionNotPresent:
    case vk::Result::eErrorFormatNotSupported:
    case vk::Result::eErrorIncompatibleDriver:
    case vk::Result::eErrorLayerNotPresent:
    case vk::Result::eErrorIncompatibleDisplayKHR:
    case vk::Result::eErrorImageUsageNotSupportedKHR:
    case vk::Result::eErrorVideoPictureLayoutNotSupportedKHR:
    case vk::Result::eErrorVideoProfileOperationNotSupportedKHR:
    case vk::Result::eErrorVideoProfileFormatNotSupportedKHR:
    case vk::Result::eErrorVideoProfileCodecNotSupportedKHR:
    case vk::Result::eErrorVideoStdVersionNotSupportedKHR:
    case vk::Result::eIncompatibleShaderBinaryEXT:
        return ErrorCode::Unsupported;
    case vk::Result::eErrorOutOfHostMemory:
    case vk::Result::eErrorOutOfDeviceMemory:
    case vk::Result::eErrorOutOfPoolMemory:
    case vk::Result::eErrorFragmentedPool:
    case vk::Result::eErrorTooManyObjects:
    case vk::Result::eErrorFragmentation:
    case vk::Result::eErrorCompressionExhaustedEXT:
    case vk::Result::eErrorPresentTimingQueueFullEXT:
    case vk::Result::eErrorNotEnoughSpaceKHR:
        return ErrorCode::ResourceExhausted;
    case vk::Result::eErrorSurfaceLostKHR:
        return ErrorCode::SurfaceLost;
    case vk::Result::eErrorDeviceLost:
        return ErrorCode::DeviceLost;
    case vk::Result::eErrorInvalidExternalHandle:
    case vk::Result::eErrorInvalidOpaqueCaptureAddress:
    case vk::Result::eErrorInvalidVideoStdParametersKHR:
    case vk::Result::eErrorNativeWindowInUseKHR:
    case vk::Result::eErrorInvalidShaderNV:
    case vk::Result::eErrorInvalidDrmFormatModifierPlaneLayoutEXT:
        return ErrorCode::InvalidArgument;
    case vk::Result::eSuccess:
    case vk::Result::eNotReady:
    case vk::Result::eEventSet:
    case vk::Result::eEventReset:
    case vk::Result::eIncomplete:
    case vk::Result::eErrorInitializationFailed:
    case vk::Result::eErrorMemoryMapFailed:
    case vk::Result::eErrorUnknown:
    case vk::Result::eErrorValidationFailed:
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eSuboptimalKHR:
    case vk::Result::ePipelineCompileRequired:
    case vk::Result::eErrorNotPermitted:
    case vk::Result::ePipelineBinaryMissingKHR:
    case vk::Result::eThreadIdleKHR:
    case vk::Result::eThreadDoneKHR:
    case vk::Result::eOperationDeferredKHR:
    case vk::Result::eOperationNotDeferredKHR:
        return ErrorCode::BackendFailure;
    }
    return ErrorCode::BackendFailure;
}

auto errorMessage(ErrorCode code, vk::Result result) -> std::string
{
    auto message = std::string {};
    switch (code)
    {
    case ErrorCode::Unsupported:
        message = "A required Vulkan capability is unavailable";
        break;
    case ErrorCode::ResourceExhausted:
        message = "Vulkan resources are exhausted";
        break;
    case ErrorCode::Timeout:
        message = "A Vulkan operation timed out";
        break;
    case ErrorCode::SurfaceLost:
        message = "The Vulkan surface was lost";
        break;
    case ErrorCode::DeviceLost:
        message = "The Vulkan device was lost";
        break;
    case ErrorCode::InvalidArgument:
    case ErrorCode::BackendFailure:
        message = "A Vulkan operation failed";
        break;
    }
    message += " (" + vk::to_string(result) + ")";
    return message;
}
}

namespace detail
{
auto makeVulkanError(vk::Result result, std::source_location source) -> Error
{
    const auto code = classify(result);
    return makeError(code,
                     errorMessage(code, result),
                     NativeError {.api = "Vulkan", .code = std::to_underlying(result)},
                     source);
}
}

auto makeVulkanError(std::int32_t result, std::source_location source) -> Error
{
    return detail::makeVulkanError(static_cast<vk::Result>(result), source);
}
}

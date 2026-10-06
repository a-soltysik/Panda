#pragma once

/// @file
/// Shared recoverable error and result types.

#include <cstdint>
#include <expected>
#include <optional>
#include <source_location>
#include <string>

namespace panda
{

/// @brief Recovery-oriented category for a failed engine operation.
enum class ErrorCode : std::uint8_t
{
    Unsupported,
    InvalidArgument,
    ResourceExhausted,
    Timeout,
    SurfaceLost,
    DeviceLost,
    BackendFailure
};

/// @brief Native status attached to an engine error when a backend supplies one.
struct NativeError
{
    /// Native API name, such as Vulkan or GLFW.
    std::string api;
    /// Native status value, preserving its signed representation.
    std::int64_t code {0};
};

/// @brief Recoverable engine failure with optional backend diagnostics.
struct Error
{
    /// Recovery-oriented category for application decisions.
    ErrorCode code {ErrorCode::BackendFailure};
    /// Human-readable cause and suggested response where known.
    std::string message;
    /// Backend-specific status for detailed logging or diagnosis.
    std::optional<NativeError> native;
    /// Source location where this error was created; intended for diagnostics.
    std::source_location source;
};

/// @brief Checked result used by fallible Panda operations.
template <typename Value>
using Result = std::expected<Value, Error>;

/// @brief Creates an engine error and captures the call site by default.
/// @param code Recovery-oriented category.
/// @param message Human-readable cause.
/// @param native Optional backend status.
/// @param source Error origin; callers normally use the default.
/// @return Owned diagnostic with no references to temporary text.
[[nodiscard]] auto makeError(ErrorCode code,
                             std::string message,
                             std::optional<NativeError> native = std::nullopt,
                             std::source_location source = std::source_location::current()) -> Error;

/// @brief Converts a Vulkan status into an application error.
/// @param result Signed VkResult value; call only for a failed result.
/// @param source Call site, captured automatically by default.
/// @return Recovery category, readable message, raw Vulkan code and call site.
/// Unknown values default to BackendFailure; the Vulkan result switch is kept
/// exhaustive so SDK additions require a deliberate recovery-category decision.
[[nodiscard]] auto makeVulkanError(std::int32_t result, std::source_location source = std::source_location::current())
    -> Error;

}

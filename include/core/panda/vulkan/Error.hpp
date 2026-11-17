#pragma once

/// @file
/// Public conversion from Vulkan status values to Panda errors.

#include <panda/base/Error.hpp>
#include <source_location>
#include <vulkan/vulkan.hpp>

namespace panda
{

/// @brief Converts a Vulkan status into an application error.
/// @param result Vulkan-Hpp result status; call only for a failed result.
/// @param source Call site, captured automatically by default.
/// @return Recovery category, readable message, raw Vulkan code and call site.
/// Unknown values default to BackendFailure; the Vulkan result switch is kept
/// exhaustive so SDK additions require a deliberate recovery-category decision.
[[nodiscard]] auto makeVulkanError(vk::Result result, std::source_location source = std::source_location::current())
    -> Error;

}

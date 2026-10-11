#pragma once

#include <panda/Assert.hpp>

// The linked Vulkan loader supplies vkGetInstanceProcAddr, including in unit
// tests. Do not open a second loader behind the test/library boundary.
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0  // NOLINT(cppcoreguidelines-macro-usage)

// Vulkan-Hpp checks both internal invariants and VkResult values with assertions.
// Preserve internal checks in debug builds; callers inspect VkResult, including
// recoverable WSI statuses. Its release dispatcher omits assertion helpers.
#ifndef NDEBUG
#    define VULKAN_HPP_ASSERT(condition) ::panda::expect((condition), #condition)
#else
#    define VULKAN_HPP_ASSERT(condition) static_cast<void>(0)
#endif
#define VULKAN_HPP_ASSERT_ON_RESULT(condition) static_cast<void>(condition)  // NOLINT(cppcoreguidelines-macro-usage)

#include <vulkan/vulkan.hpp>            // IWYU pragma: export
#include <vulkan/vulkan_raii.hpp>       // IWYU pragma: export
#include <vulkan/vulkan_to_string.hpp>  // IWYU pragma: export

#pragma once

#include <panda/Assert.hpp>

// The linked Vulkan loader supplies vkGetInstanceProcAddr; do not open another loader.
// Keep the first Vulkan-Hpp configuration in unity translation units.
#ifndef VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
#    define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#ifndef VULKAN_HPP_ASSERT
// Preserve internal checks in debug builds; callers inspect VkResult, including
// recoverable WSI statuses. Its release dispatcher omits assertion helpers.
#    ifndef NDEBUG
#        define VULKAN_HPP_ASSERT(condition) ::panda::expect((condition), #condition)
#    else
#        define VULKAN_HPP_ASSERT(condition) static_cast<void>(0)
#    endif
#endif
#ifndef VULKAN_HPP_ASSERT_ON_RESULT
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#    define VULKAN_HPP_ASSERT_ON_RESULT(condition) static_cast<void>(condition)
#endif

#include <vulkan/vulkan.hpp>            // IWYU pragma: export
#include <vulkan/vulkan_raii.hpp>       // IWYU pragma: export
#include <vulkan/vulkan_to_string.hpp>  // IWYU pragma: export

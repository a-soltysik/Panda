#pragma once

/// @file
/// Core package version query.

#include <string_view>

namespace panda
{

/// @brief Returns the version compiled into the Panda core library.
/// This query does not initialize Vulkan or CUDA.
/// @return A view of a string literal with static lifetime.
[[nodiscard]] auto version() noexcept -> std::string_view;

}

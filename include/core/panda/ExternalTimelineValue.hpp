#pragma once

/// @file
/// Strongly typed value for an external timeline semaphore.

#include <cstdint>
#include <panda/StrongAlias.hpp>

namespace panda
{
namespace detail
{
struct ExternalTimelineValueTag;
}

/// @brief Value used to order work across an external GPU API boundary.
using ExternalTimelineValue = StrongAlias<std::uint64_t, detail::ExternalTimelineValueTag>;
}

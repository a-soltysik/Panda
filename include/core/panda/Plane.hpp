#pragma once

/// @file
/// CPU geometry for a unit plane.

#include <panda/Geometry.hpp>

namespace panda
{
/// @brief Creates a one-meter square in the XZ plane, centered at the origin and facing +Y.
/// @return Owned CPU data that can be uploaded to a Mesh; may allocate.
[[nodiscard]] auto createPlaneGeometry() -> Geometry;
}

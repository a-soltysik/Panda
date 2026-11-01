#pragma once

/// @file
/// CPU geometry for the standard unit cube.

#include <panda/Geometry.hpp>

namespace panda
{
/// @brief Creates a cube centered at the origin with side length one meter.
/// Each face has independent vertices and counterclockwise triangles viewed from outside.
/// @return Owned CPU data that can be uploaded to a Mesh; may allocate.
[[nodiscard]] auto createCubeGeometry() -> Geometry;
}

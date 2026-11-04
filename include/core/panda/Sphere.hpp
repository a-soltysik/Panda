#pragma once

/// @file
/// CPU geometry for a unit sphere.

#include <cstdint>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>

namespace panda
{
/// @brief Tessellation chosen once when creating sphere geometry.
struct SphereGeometryOptions
{
    /// Number of segments around the vertical axis; must be at least three.
    std::uint32_t longitudeSegments {32};
    /// Number of divisions from the north pole to the south pole; must be at least two.
    std::uint32_t latitudeRings {16};
};

/// @brief Creates a sphere centered at the origin with radius 0.5 meters.
/// Reuse the resulting geometry and mesh instead of rebuilding it every frame.
/// @param options Mesh tessellation; defaults to 32 longitude segments and 16 latitude rings.
/// @return Owned CPU data that can be uploaded to a Mesh; InvalidArgument for insufficient
/// tessellation or a vertex/index count that cannot be represented by Mesh; may allocate.
[[nodiscard]] auto createSphereGeometry(const SphereGeometryOptions& options = {}) -> Result<Geometry>;
}

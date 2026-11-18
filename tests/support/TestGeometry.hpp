#pragma once

#include <cstdint>
#include <panda/geometry/Geometry.hpp>
#include <vector>

namespace panda::test
{
[[nodiscard]] inline auto makeTestTriangleGeometry() -> Geometry
{
    return Geometry {
        .vertices =
            {{.position = {-0.5F, -0.5F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .tangent = {1.0F, 0.0F, 0.0F, 1.0F}},
                       {.position = {0.5F, -0.5F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .tangent = {1.0F, 0.0F, 0.0F, 1.0F}},
                       {.position = {0.0F, 0.5F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .tangent = {1.0F, 0.0F, 0.0F, 1.0F}}        },
        .indices = std::vector<std::uint16_t> {0,                                                                                                     1, 2}
    };
}
}

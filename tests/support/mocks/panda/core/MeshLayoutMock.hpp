#pragma once

#include <gmock/gmock.h>

#include <cstdint>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

#include "MeshLayout.hpp"

namespace panda::test
{
struct MeshLayoutPlan
{
    panda::MeshDesc description;
    std::uint64_t vertexSize {0};
    std::uint64_t indexSize {0};
};

class MeshLayoutMock
{
public:
    using MeshLayout = panda::core::vulkan::resources::MeshLayout;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<MeshLayoutPlan>, create, (const panda::MeshDesc&), ());
    // cppcheck cannot parse gMock declarations with resource references.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(std::uint32_t, getVertexCount, (const MeshLayout&), (const));
    MOCK_METHOD(std::uint32_t, getVertexStride, (const MeshLayout&), (const));
    MOCK_METHOD(std::uint64_t, getVertexSize, (const MeshLayout&), (const));
    MOCK_METHOD(const std::optional<panda::MeshIndexDesc>&, getIndices, (const MeshLayout&), (const));
    MOCK_METHOD(std::uint64_t, getIndexSize, (const MeshLayout&), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

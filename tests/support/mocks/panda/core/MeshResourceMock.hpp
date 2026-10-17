#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <memory>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>

#include "BufferResource.hpp"
#include "MeshLayout.hpp"
#include "MeshResource.hpp"

namespace panda::test
{
struct MeshResourcePlan
{
    std::shared_ptr<panda::detail::BufferResource> vertexBuffer;
    std::shared_ptr<panda::detail::BufferResource> indexBuffer;
    const panda::core::vulkan::resources::MeshLayout* layout {nullptr};
};

class MeshResourceMock
{
public:
    using BufferResource = panda::detail::BufferResource;
    using BufferResourceRef = std::shared_ptr<BufferResource>;
    using MeshLayout = panda::core::vulkan::resources::MeshLayout;
    using MeshResource = panda::detail::MeshResource;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<MeshResourcePlan>,
                create,
                (const vk::raii::Device&, panda::core::vulkan::resources::MemoryPool&, const panda::MeshDesc&),
                ());
    // cppcheck's gMock parser cannot model const-reference returns.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(const BufferResourceRef&, getVertexBuffer, (const MeshResource&), (const));
    // cppcheck-suppress syntaxError
    MOCK_METHOD(const BufferResourceRef&, getIndexBuffer, (const MeshResource&), (const));
    // cppcheck-suppress syntaxError
    MOCK_METHOD(const MeshLayout&, getLayout, (const MeshResource&), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}

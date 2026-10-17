#include <cstdint>
#include <exception>
#include <expected>
#include <optional>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <utility>

#include "MeshLayout.hpp"
#include "ScopedMock.hpp"
#include "panda/core/MeshLayoutMock.hpp"

namespace panda::core::vulkan::resources
{
namespace
{
template <typename Function>
auto invokeNoexcept(Function&& function) noexcept -> decltype(std::forward<Function>(function)())
{
    try
    {
        return std::forward<Function>(function)();
    }
    catch (...)
    {
        std::terminate();
    }
}
}

auto MeshLayout::create(const MeshDesc& description) -> Result<MeshLayout>
{
    auto plan = test::ScopedMock<test::MeshLayoutMock>::getActiveMock().create(description);
    if (!plan)
    {
        return std::unexpected {plan.error()};
    }
    auto layout = MeshLayout {};
    layout._description = plan->description;
    layout._vertexSize = plan->vertexSize;
    layout._indexSize = plan->indexSize;
    return layout;
}

auto MeshLayout::getVertexCount() const noexcept -> std::uint32_t
{
    return invokeNoexcept([&] {
        return test::ScopedMock<test::MeshLayoutMock>::getActiveMock().getVertexCount(*this);
    });
}

auto MeshLayout::getVertexStride() const noexcept -> std::uint32_t
{
    return invokeNoexcept([&] {
        return test::ScopedMock<test::MeshLayoutMock>::getActiveMock().getVertexStride(*this);
    });
}

auto MeshLayout::getVertexSize() const noexcept -> std::uint64_t
{
    return invokeNoexcept([&] {
        return test::ScopedMock<test::MeshLayoutMock>::getActiveMock().getVertexSize(*this);
    });
}

auto MeshLayout::getIndices() const noexcept -> const std::optional<MeshIndexDesc>&
{
    return invokeNoexcept([&]() -> const std::optional<MeshIndexDesc>& {
        return test::ScopedMock<test::MeshLayoutMock>::getActiveMock().getIndices(*this);
    });
}

auto MeshLayout::getIndexSize() const noexcept -> std::uint64_t
{
    return invokeNoexcept([&] {
        return test::ScopedMock<test::MeshLayoutMock>::getActiveMock().getIndexSize(*this);
    });
}
}

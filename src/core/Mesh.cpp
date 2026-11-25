// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <panda/geometry/VertexLayout.hpp>
#include <panda/resources/Mesh.hpp>
#include <span>
#include <utility>

#include "vulkan/resources/MeshResource.hpp"

namespace panda
{
Mesh::Mesh() = default;
Mesh::Mesh(const Mesh& other) = default;
auto Mesh::operator=(const Mesh& other) -> Mesh& = default;
Mesh::Mesh(Mesh&& other) noexcept = default;
auto Mesh::operator=(Mesh&& other) noexcept -> Mesh& = default;
Mesh::~Mesh() = default;

Mesh::Mesh(std::shared_ptr<detail::MeshResource> resource)
    : _resource {std::move(resource)}
{
}

auto Mesh::isValid() const noexcept -> bool
{
    return _resource != nullptr;
}

auto Mesh::operator==(const Mesh& other) const noexcept -> bool
{
    return _resource == other._resource;
}

auto Mesh::getVertexCount() const noexcept -> std::uint32_t
{
    return _resource ? _resource->getLayout().getVertexCount() : 0;
}

auto Mesh::getVertexStride() const noexcept -> std::uint32_t
{
    return _resource ? _resource->getLayout().getVertexStride() : 0;
}

auto Mesh::getVertexAttributes() const noexcept -> std::span<const VertexAttribute>
{
    return _resource ? std::span {_resource->getLayout().getVertexAttributes()} : std::span<const VertexAttribute> {};
}

auto Mesh::getVertexSize() const noexcept -> std::uint64_t
{
    return _resource ? _resource->getLayout().getVertexSize() : 0;
}

auto Mesh::getIndexCount() const noexcept -> std::uint32_t
{
    return _resource && _resource->getLayout().getIndices() ? _resource->getLayout().getIndices()->count : 0;
}

auto Mesh::getIndexType() const noexcept -> std::optional<MeshIndexType>
{
    return _resource && _resource->getLayout().getIndices() ? std::optional {_resource->getLayout().getIndices()->type}
                                                            : std::nullopt;
}

auto Mesh::getIndexSize() const noexcept -> std::uint64_t
{
    return _resource ? _resource->getLayout().getIndexSize() : 0;
}

void Mesh::reset() noexcept
{
    _resource.reset();
}
}

namespace std
{
auto hash<panda::Mesh>::operator()(const panda::Mesh& mesh) const noexcept -> std::size_t
{
    return hash<const void*> {}(mesh._resource.get());
}
}

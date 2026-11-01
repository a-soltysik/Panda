// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "upload/UploadBatchImpl.hpp"

namespace panda
{
UploadBatch::UploadBatch() = default;

UploadBatch::UploadBatch(std::unique_ptr<detail::UploadBatchImpl> impl)
    : _impl {std::move(impl)}
{
}

UploadBatch::UploadBatch(UploadBatch&& other) noexcept = default;
auto UploadBatch::operator=(UploadBatch&& other) noexcept -> UploadBatch& = default;
UploadBatch::~UploadBatch() = default;

auto UploadBatch::isValid() const noexcept -> bool
{
    return _impl != nullptr;
}

auto UploadBatch::write(const Buffer& destination, std::uint64_t offset, std::span<const std::byte> bytes)
    -> Result<void>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    return _impl->write(detail::BufferResource::getResource(destination), offset, bytes);
}

auto UploadBatch::write(const Mesh& destination,
                        std::span<const std::byte> vertexBytes,
                        std::span<const std::byte> indexBytes) -> Result<void>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    return _impl->write(detail::MeshResource::getResource(destination), vertexBytes, indexBytes);
}

auto UploadBatch::write(const Mesh& destination, const Geometry& geometry) -> Result<void>
{
    if (geometry.indices.valueless_by_exception())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Geometry indices have no value")};
    }
    if (destination.getVertexStride() != standardMeshVertexStrideBytes)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh does not match standard geometry")};
    }
    return std::visit(
        [this, &destination, &geometry]<typename Index>(const std::vector<Index>& indices) -> Result<void> {
            const auto type = std::is_same_v<Index, std::uint16_t> ? MeshIndexType::UInt16 : MeshIndexType::UInt32;
            if (!indices.empty() && destination.getIndexType() != type)
            {
                return std::unexpected {
                    makeError(ErrorCode::InvalidArgument, "Mesh index type does not match geometry")};
            }
            if (std::ranges::any_of(indices, [&geometry](auto index) {
                    return index >= geometry.vertices.size();
                }))
            {
                return std::unexpected {
                    makeError(ErrorCode::InvalidArgument, "Geometry index is outside the vertex array")};
            }
            return write(destination, std::as_bytes(std::span {geometry.vertices}), std::as_bytes(std::span {indices}));
        },
        geometry.indices);
}

auto UploadBatch::write(const Texture& destination, std::span<const std::byte> bytes, std::uint64_t rowPitchBytes)
    -> Result<void>
{
    const auto extent = destination.getExtent();
    return write(destination, bytes, TextureUploadRegion {.extent = extent, .rowPitchBytes = rowPitchBytes});
}

auto UploadBatch::write(const Texture& destination, std::span<const std::byte> bytes, const TextureUploadRegion& region)
    -> Result<void>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    return _impl->write(detail::TextureResource::getResource(destination), bytes, region);
}

auto UploadBatch::submit() -> Result<CompletionPoint>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    auto submitted = _impl->submit();
    if (submitted)
    {
        _impl.reset();
    }
    return submitted;
}
}

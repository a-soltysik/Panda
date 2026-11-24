// clang-format off
#include "vulkan/VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/geometry/VertexLayout.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/ResourceManager.hpp>
#include <panda/resources/Texture.hpp>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "vulkan/renderer/ShaderProgramState.hpp"
#include "vulkan/resources/BufferResource.hpp"
#include "vulkan/resources/MeshResource.hpp"
#include "vulkan/resources/OffscreenTargetResource.hpp"
#include "vulkan/resources/TextureResource.hpp"
#include "vulkan/resources/memory/MemoryPool.hpp"
#include "vulkan/upload/UploadManager.hpp"

namespace panda
{
namespace
{
template <typename Resource, typename Description>
auto createResource(const vk::raii::Device& device,
                    core::vulkan::resources::MemoryPool& pool,
                    core::vulkan::upload::UploadManager& uploads,
                    const Description& description) -> Result<std::shared_ptr<Resource>>
{
    return uploads.collect().and_then([&] {
        return Resource::create(device, pool, description);
    });
}
}

ResourceManager::ResourceManager(const vk::raii::Device& device,
                                 core::vulkan::resources::MemoryPool& pool,
                                 core::vulkan::upload::UploadManager& uploads) noexcept
    : _device {device},
      _pool {pool},
      _uploads {uploads}
{
}

auto ResourceManager::createBuffer(const BufferDesc& description) const -> Result<Buffer>
{
    return createResource<detail::BufferResource>(_device, _pool, _uploads, description)
        .transform([](std::shared_ptr<detail::BufferResource> resource) {
            return Buffer {std::move(resource)};
        });
}

auto ResourceManager::createTexture(const TextureDesc& description) const -> Result<Texture>
{
    return createResource<detail::TextureResource>(_device, _pool, _uploads, description)
        .transform([](std::shared_ptr<detail::TextureResource> resource) {
            return Texture {std::move(resource)};
        });
}

auto ResourceManager::createOffscreenTarget(const OffscreenTargetDesc& description) const -> Result<OffscreenTarget>
{
    return createResource<detail::OffscreenTargetResource>(_device, _pool, _uploads, description)
        .transform([](std::shared_ptr<detail::OffscreenTargetResource> resource) {
            return OffscreenTarget {std::move(resource)};
        });
}

auto ResourceManager::createMesh(const MeshDesc& description) const -> Result<Mesh>
{
    return createResource<detail::MeshResource>(_device, _pool, _uploads, description)
        .transform([](std::shared_ptr<detail::MeshResource> resource) {
            return Mesh {std::move(resource)};
        });
}

auto ResourceManager::createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>
{
    if (geometry.indices.valueless_by_exception() ||
        geometry.vertices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Geometry cannot be represented by a Mesh")};
    }
    const auto [indexCount, indexType] = std::visit(
        []<typename Index>(const std::vector<Index>& indices) {
            const auto type = std::is_same_v<Index, std::uint16_t> ? MeshIndexType::UInt16 : MeshIndexType::UInt32;
            return std::pair {indices.size(), type};
        },
        geometry.indices);
    if (indexCount > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Geometry has too many indices")};
    }
    auto indexDescription = std::optional<MeshIndexDesc> {};
    if (indexCount != 0)
    {
        indexDescription = MeshIndexDesc {.count = static_cast<std::uint32_t>(indexCount), .type = indexType};
    }
    return createMesh({.vertexCount = static_cast<std::uint32_t>(geometry.vertices.size()),
                       .vertexStride = standardMeshVertexStrideBytes,
                       .indices = indexDescription,
                       .attributes = std::ranges::to<std::vector<VertexAttribute>>(standardMeshVertexAttributes)});
}

auto ResourceManager::createShaderProgram(const ProgramDesc& description) const -> Result<ShaderProgram>
{
    return ShaderProgram::create(_device, description);
}

auto ResourceManager::reloadShaderProgram(const ShaderProgram& program, const ProgramDesc& description) const
    -> Result<void>
{
    const auto current = program.snapshot();
    if (!current || current->device != &_device)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shader program belongs to another Engine")};
    }
    return ShaderProgram::create(_device, description).and_then([&](const auto& replacement) -> Result<void> {
        const auto next = replacement.snapshot();
        if (current->vertexInput != next->vertexInput ||
            !core::vulkan::renderer::sameShaderProgramInterface(current->camera,
                                                                current->shadow,
                                                                next->camera,
                                                                next->shadow))
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Shader program reload changes its reflected interface")};
        }
        program._identity->current.store(next, std::memory_order_release);
        return {};
    });
}
}

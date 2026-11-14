#include <panda/Buffer.hpp>
#include <panda/Engine.hpp>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/ResourceManager.hpp>
#include <panda/Texture.hpp>

namespace panda
{
ResourceManager::ResourceManager(const Engine& context) noexcept
    : _engine {&context}
{
}

auto ResourceManager::createBuffer(const BufferDesc& description) const -> Result<Buffer>
{
    return _engine->createBuffer(description);
}

auto ResourceManager::createTexture(const TextureDesc& description) const -> Result<Texture>
{
    return _engine->createTexture(description);
}

auto ResourceManager::createOffscreenTarget(const OffscreenTargetDesc& description) const -> Result<OffscreenTarget>
{
    return _engine->createOffscreenTarget(description);
}

auto ResourceManager::createMesh(const MeshDesc& description) const -> Result<Mesh>
{
    return _engine->createMesh(description);
}

auto ResourceManager::createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>
{
    return _engine->createMeshFromGeometry(geometry);
}
}

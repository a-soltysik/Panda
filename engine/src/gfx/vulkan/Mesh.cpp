#include "panda/gfx/vulkan/Mesh.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <numeric>

#include "panda/gfx/vulkan/Context.h"

namespace
{

auto getIndices(const aiMesh& mesh, std::vector<uint32_t>& indices, uint32_t offset = 0) -> void
{
    const auto faces = std::span(mesh.mFaces, mesh.mNumFaces);
    for (const auto& face : faces)
    {
        const auto faceIndices = std::span(face.mIndices, face.mNumIndices);
        indices.push_back(faceIndices[0] + offset);
        indices.push_back(faceIndices[1] + offset);
        indices.push_back(faceIndices[2] + offset);
    }
}

auto getVertices(const aiMesh& mesh, std::vector<panda::gfx::vulkan::Vertex>& vertices) -> void
{
    const auto meshVertices = std::span(mesh.mVertices, mesh.mNumVertices);
    const auto meshNormals = std::span(mesh.mNormals, mesh.mNumVertices);
    for (auto i = uint32_t {}; i < mesh.mNumVertices; i++)
    {
        const auto vertex = panda::gfx::vulkan::Vertex {
            .position = {meshVertices[i].x, meshVertices[i].y, meshVertices[i].z},
            .normal = {meshNormals[i].x,  meshNormals[i].y,  meshNormals[i].z }
        };
        vertices.push_back(vertex);
    }
}

auto getProperName(const aiScene& scene) -> std::string
{
    if (scene.mNumMeshes == 1 || (scene.mNumMeshes > 1 && scene.mName.length == 0))
    {
        return std::span(scene.mMeshes, scene.mNumMeshes).front()->mName.C_Str();
    }
    return scene.mName.C_Str();
}

}

namespace panda::gfx::vulkan
{

Mesh::Mesh(const std::string& name,
           const Device& device,
           std::span<const Vertex> vertices,
           std::span<const uint32_t> indices)
    : _device {device},
      _name {name},
      _vertexBuffer {createVertexBuffer(_device, vertices)},
      _indexBuffer {createIndexBuffer(_device, indices)},
      _vertexCount {static_cast<uint32_t>(vertices.size())},
      _indexCount {static_cast<uint32_t>(indices.size())}
{
    log::Info("Created Mesh with {} vertices and {} indices", _vertexCount, _indexCount);
}

auto Mesh::createVertexBuffer(const vulkan::Device& device, std::span<const vulkan::Vertex> vertices)
    -> std::unique_ptr<vulkan::Buffer>
{
    expect(
        vertices.size(),
        [](const auto size) {
            return size >= 3;
        },
        "Vertices size should be greater or equal to 3");

    const auto stagingBuffer =
        vulkan::Buffer {device,
                        vertices,
                        vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newVertexBuffer =
        std::make_unique<vulkan::Buffer>(device,
                                         stagingBuffer.size,
                                         vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

    vulkan::Buffer::copy(stagingBuffer, *newVertexBuffer);

    return newVertexBuffer;
}

auto Mesh::bind(const vk::CommandBuffer& commandBuffer) const -> void
{
    commandBuffer.bindVertexBuffers(0, _vertexBuffer->buffer, {0});

    if (_indexBuffer != nullptr)
    {
        commandBuffer.bindIndexBuffer(_indexBuffer->buffer, 0, vk::IndexType::eUint32);
    }
}

auto Mesh::draw(const vk::CommandBuffer& commandBuffer) const -> void
{
    if (_indexBuffer != nullptr)
    {
        commandBuffer.drawIndexed(_indexCount, 1, 0, 0, 0);
    }
    else
    {
        commandBuffer.draw(_vertexCount, 1, 0, 0);
    }
}

auto Mesh::createIndexBuffer(const vulkan::Device& device, const std::span<const uint32_t> indices)
    -> std::unique_ptr<vulkan::Buffer>
{
    if (indices.empty())
    {
        return nullptr;
    }

    if (!shouldBe(
            indices.size(),
            [](const auto size) {
                return size >= 3;
            },
            "Indices  size should be greater or equal to 3"))
    {
        return nullptr;
    }

    const auto stagingBuffer =
        vulkan::Buffer {device,
                        indices,
                        vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newIndexBuffer =
        std::make_unique<vulkan::Buffer>(device,
                                         stagingBuffer.size,
                                         vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

    vulkan::Buffer::copy(stagingBuffer, *newIndexBuffer);

    return newIndexBuffer;
}

auto Mesh::getName() const noexcept -> const std::string&
{
    return _name;
}

auto Mesh::loadMesh(Context& context, const std::filesystem::path& path) -> Mesh*
{
    const auto pathStr = path.string();
    auto importer = Assimp::Importer {};
    const auto* const scene =
        importer.ReadFile(pathStr.c_str(),
                          aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!shouldNotBe(scene,
                     nullptr,
                     fmt::format("Error during opening \"{}\" file: {}", pathStr.c_str(), importer.GetErrorString())))
    {
        return nullptr;
    }

    auto vertices = std::vector<vulkan::Vertex> {};
    auto indices = std::vector<uint32_t> {};
    auto offset = uint32_t {};

    const auto meshes = std::span(scene->mMeshes, scene->mNumMeshes);
    for (const auto* currentMesh : meshes)
    {
        getIndices(*currentMesh, indices, offset);
        getVertices(*currentMesh, vertices);

        offset += currentMesh->mNumVertices;
    }

    auto mesh = std::make_unique<Mesh>(getProperName(*scene), context.getDevice(), vertices, indices);

    auto* result = mesh.get();
    context.registerMesh(std::move(mesh));

    return result;
}

auto Mesh::loadMeshes(Context& context, const std::filesystem::path& path) -> std::vector<Mesh*>
{
    const auto pathStr = path.string();
    auto importer = Assimp::Importer {};
    const auto* scene =
        importer.ReadFile(pathStr.c_str(),
                          aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!shouldNotBe(scene,
                     nullptr,
                     fmt::format("Error during opening \"{}\" file: {}", pathStr.c_str(), importer.GetErrorString())))
    {
        return {};
    }

    auto result = std::vector<Mesh*> {};
    result.reserve(scene->mNumMeshes);

    const auto meshes = std::span(scene->mMeshes, scene->mNumMeshes);
    for (const auto* currentMesh : meshes)
    {
        auto vertices = std::vector<vulkan::Vertex> {};
        auto indices = std::vector<uint32_t> {};
        getIndices(*currentMesh, indices);
        getVertices(*currentMesh, vertices);

        auto mesh = std::make_unique<Mesh>(currentMesh->mName.C_Str(), context.getDevice(), vertices, indices);

        result.push_back(mesh.get());
        context.registerMesh(std::move(mesh));
    }
    return result;
}

}
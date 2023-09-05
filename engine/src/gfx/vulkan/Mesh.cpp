#include "panda/gfx/vulkan/Mesh.h"

#include <tiny_obj_loader.h>

namespace
{

template <typename T>
[[nodiscard]] auto getVertexAttribute2(const tinyobj::attrib_t& attribute,
                                       T tinyobj::attrib_t::*attributePointer,
                                       int index) -> glm::vec2
{
    if (index >= 0)
    {
        const auto& value = attribute.*attributePointer;
        return {
            value[2 * static_cast<uint32_t>(index) + 0],
            value[2 * static_cast<uint32_t>(index) + 1],
        };
    }
    return {};
}

template <typename T>
[[nodiscard]] auto getVertexAttribute3(const tinyobj::attrib_t& attribute,
                                       T tinyobj::attrib_t::*attributePointer,
                                       int index) -> glm::vec3
{
    if (index >= 0)
    {
        const auto& value = attribute.*attributePointer;
        return {
            value[3 * static_cast<uint32_t>(index) + 0],
            value[3 * static_cast<uint32_t>(index) + 1],
            value[3 * static_cast<uint32_t>(index) + 2],
        };
    }
    return {};
}

}

namespace panda::gfx::vulkan
{

Mesh::Mesh(const vulkan::Device& device, std::span<const vulkan::Vertex> vertices, std::span<const uint32_t> indices)
    : _device {device},
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

auto Mesh::loadMesh(const vulkan::Device& device, const std::filesystem::path& path) -> std::unique_ptr<Mesh>
{
    auto attribute = tinyobj::attrib_t {};
    auto shapes = std::vector<tinyobj::shape_t> {};
    auto materials = std::vector<tinyobj::material_t> {};
    auto warning = std::string {};
    auto error = std::string {};

    if (!shouldBe(tinyobj::LoadObj(&attribute, &shapes, &materials, &warning, &error, path.string().c_str()),
                  fmt::format("Warning: {}\nError: {}", warning, error)))
    {
        return nullptr;
    }

    shouldBe(warning.empty(), warning);
    shouldBe(error.empty(), error);

    auto vertices = std::vector<vulkan::Vertex> {};
    auto indices = std::vector<uint32_t> {};
    auto uniqueVertices = std::unordered_map<vulkan::Vertex, uint32_t> {};

    for (const auto& shape : shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            const auto vertex =
                vulkan::Vertex {getVertexAttribute3(attribute, &tinyobj::attrib_t::vertices, index.vertex_index),
                               getVertexAttribute3(attribute, &tinyobj::attrib_t::colors, index.vertex_index),
                               getVertexAttribute3(attribute, &tinyobj::attrib_t::normals, index.normal_index),
                               getVertexAttribute2(attribute, &tinyobj::attrib_t::texcoords, index.texcoord_index)};
            if (!uniqueVertices.contains(vertex))
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }
            indices.push_back(uniqueVertices[vertex]);
        }
    }

    return std::make_unique<Mesh>(device, vertices, indices);
}

}
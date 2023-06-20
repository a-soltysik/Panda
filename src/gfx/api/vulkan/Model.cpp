#include "Model.h"

namespace panda::gfx::vulkan
{

Model::Model(const Device& deviceRef, std::span<const Vertex> vertices, std::span<const uint16_t> indices)
    : device {deviceRef},
      vertexBuffer {createVertexBuffer(device, vertices)},
      indexBuffer {createIndexBuffer(device, indices)},
      indexCount {static_cast<uint32_t>(indices.size())}
{
}

auto Model::createVertexBuffer(const Device& device, std::span<const Vertex> vertices) -> std::unique_ptr<Buffer>
{
    if (!shouldBe(
            vertices.size(),
            [](const auto size) {
                return size >= 3;
            },
            "Vertices size should be greater or equal to 3"))
    {
        return nullptr;
    }

    const auto stagingBuffer =
        Buffer {device,
                vertices,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newVertexBuffer =
        std::make_unique<Buffer>(device,
                                 stagingBuffer.size,
                                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

    Buffer::copy(stagingBuffer, *newVertexBuffer);

    return newVertexBuffer;
}

auto Model::bind(const vk::CommandBuffer& commandBuffer) const -> void {
    commandBuffer.bindVertexBuffers(0, vertexBuffer->buffer, {0});
    commandBuffer.bindIndexBuffer(indexBuffer->buffer, 0, vk::IndexType::eUint16);
}

auto Model::draw(const vk::CommandBuffer& commandBuffer) const -> void {
    commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}

auto Model::createIndexBuffer(const Device& device, const std::span<const uint16_t> indices) -> std::unique_ptr<Buffer>
{
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
        Buffer {device,
                indices,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    auto newIndexBuffer =
        std::make_unique<Buffer>(device,
                                 stagingBuffer.size,
                                 vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

    Buffer::copy(stagingBuffer, *newIndexBuffer);

    return newIndexBuffer;
}

}

#pragma once

#include "Buffer.h"
#include "Device.h"
#include "Vertex.h"

namespace panda::gfx::vulkan
{

class Model
{
public:
    Model(const Device& deviceRef, std::span<const Vertex> vertices, std::span<const uint16_t> indices);

    auto bind(const vk::CommandBuffer& commandBuffer) const -> void;
    auto draw(const vk::CommandBuffer& commandBuffer) const -> void;
private:
    static auto createVertexBuffer(const Device& device, std::span<const Vertex> vertices) -> std::unique_ptr<Buffer>;
    static auto createIndexBuffer(const Device& device, std::span<const uint16_t> indices) -> std::unique_ptr<Buffer>;

    const Device& device;
    const std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;
    uint32_t indexCount;
};

}
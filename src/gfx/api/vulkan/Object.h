#pragma once

#include "Buffer.h"
#include "Device.h"
#include "Vertex.h"

namespace panda::gfx::vulkan
{

class Object
{
public:
    Object(const Device& deviceRef, const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);

    auto bind(const vk::CommandBuffer& commandBuffer) const -> void;
    auto draw(const vk::CommandBuffer& commandBuffer) const -> void;
private:
    static auto createVertexBuffer(const Device& device, const std::vector<Vertex>& vertices) -> std::unique_ptr<Buffer>;
    static auto createIndexBuffer(const Device& device, const std::vector<uint16_t>& indices) -> std::unique_ptr<Buffer>;

    const Device& device;
    const std::unique_ptr<Buffer> vertexBuffer;
    std::unique_ptr<Buffer> indexBuffer;
    uint32_t indexCount;
};

}
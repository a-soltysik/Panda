#pragma once

#include <filesystem>

#include "Texture.h"
#include "panda/gfx/vulkan/Buffer.h"
#include "panda/gfx/vulkan/Device.h"
#include "panda/gfx/vulkan/Vertex.h"

namespace panda::gfx::vulkan
{

class Context;

class Mesh
{
public:
    Mesh(std::string name,
         const Device& device,
         std::span<const Vertex> vertices,
         std::span<const uint32_t> indices = {});

    auto bind(const vk::CommandBuffer& commandBuffer) const -> void;
    auto draw(const vk::CommandBuffer& commandBuffer) const -> void;

    [[nodiscard]] auto getName() const noexcept -> const std::string&;

private:
    static auto createVertexBuffer(const Device& device, std::span<const Vertex> vertices) -> std::unique_ptr<Buffer>;
    static auto createIndexBuffer(const Device& device, std::span<const uint32_t> indices) -> std::unique_ptr<Buffer>;

    const Device& _device;
    std::string _name;
    std::unique_ptr<Buffer> _vertexBuffer;
    std::unique_ptr<Buffer> _indexBuffer;
    uint32_t _vertexCount;
    uint32_t _indexCount;
};

}
#pragma once

#include <filesystem>

#include "Buffer.h"
#include "Device.h"
#include "Vertex.h"

namespace panda::gfx::vulkan
{

class Context;

class Mesh
{
public:
    [[nodiscard]] static auto loadMesh(Context& context, const std::filesystem::path& path) -> Mesh*;
    [[nodiscard]] static auto loadMeshes(Context& context, const std::filesystem::path& path) -> std::vector<Mesh*>;

    Mesh(const std::string& name,
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
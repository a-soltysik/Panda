#pragma once

/// @file
/// Application-owned uniform values, GPU buffers and sampled textures for a shader program.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/Texture.hpp>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace panda
{
/// @brief Owning copy of an application uniform block in its GLSL layout.
struct ShaderUniformBlock
{
    /// Bytes including GLSL layout padding, typically copied from a trivially copyable C++ struct.
    std::vector<std::byte> bytes;
};

/// @brief Read-only shader access to a range of a same-Engine storage buffer.
struct ShaderBufferRange
{
    /// Buffer with Storage usage. The application orders its producers before shader reads.
    Buffer buffer;
    /// Byte offset respecting the device storage-buffer alignment.
    std::uint64_t offset {};
    /// Nonzero size in bytes. The application bounds accesses to runtime arrays.
    std::uint64_t size {};
};

/// @brief One application resource addressed by its reflected GLSL instance name.
struct ShaderResourceBinding
{
    /// GLSL descriptor instance name preserved in SPIR-V reflection.
    std::string name;
    /// Uniform bytes, a read-only storage range, or an uploaded sampled 2D/cube texture.
    std::variant<ShaderUniformBlock, ShaderBufferRange, Texture> resource;
};

/// @brief Immutable owning snapshot of application bindings, validated against the draw's program.
class ShaderResources final
{
public:
    /// @brief Creates an empty snapshot.
    ShaderResources() = default;

    /// @brief Takes ownership of the bindings and their contents. Copies of this snapshot share ownership.
    explicit ShaderResources(std::vector<ShaderResourceBinding> bindings);

    /// @brief Borrows the snapshot entries while an owning copy remains alive.
    [[nodiscard]] auto getBindings() const noexcept -> std::span<const ShaderResourceBinding>;

private:
    std::shared_ptr<const std::vector<ShaderResourceBinding>> _bindings;
};
}

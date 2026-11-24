#pragma once

/// @file
/// Engine-bound ownership of a validated graphics shader program.

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <span>
#include <string_view>
#include <variant>

namespace vk::raii
{
class Device;
}

namespace panda
{
class ResourceManager;

namespace core::vulkan::renderer
{
class ApplicationShaderBindings;
class MeshPipelineCache;
}

/// @brief Graphics stages accepted by the program factory.
enum class ShaderStage : std::uint8_t
{
    Vertex,   ///< Rasterization vertex stage.
    Fragment  ///< Rasterization fragment stage.
};

/// @brief Borrowed validated SPIR-V and its diagnostic identity.
struct ShaderCode
{
    std::span<const std::uint32_t> words;  ///< Aligned SPIR-V words valid through the factory call.
    ShaderStage stage;                     ///< Stage that must match the reflected main entry point.
    std::string_view label;                ///< Application name or embedded resource key for diagnostics.
};

/// @brief Vertex input contract for the camera and any explicit shadow stages.
enum class ShaderVertexInput : std::uint8_t
{
    StandardMesh,  ///< Vertex attributes are supplied by a Panda mesh.
    Procedural     ///< The vertex stage produces vertices without mesh attributes.
};

/// @brief Shadow selection when the application supplies no shadow code.
enum class ShaderShadowMode : std::uint8_t
{
    None,         ///< The program has no shadow variant.
    StandardMesh  ///< The built-in, undeformed mesh shadow variant is suitable.
};

/// @brief Application stages used when rendering the program into shadow maps.
struct ShaderShadowStages
{
    ShaderCode vertex;                   ///< Required shadow vertex stage.
    std::optional<ShaderCode> fragment;  ///< Optional depth/discard stage.
};

/// @brief Stages and draw contracts for one graphics program.
struct ProgramDesc
{
    ShaderCode vertex;    ///< Camera vertex stage.
    ShaderCode fragment;  ///< Camera fragment stage.
    ShaderVertexInput vertexInput {
        ShaderVertexInput::StandardMesh};                       ///< Contract shared by camera and shadow variants.
    std::variant<ShaderShadowMode, ShaderShadowStages> shadow;  ///< Choose built-in, explicit or no shadows.
};

/// @brief Copyable owner of a program identity. Engine outlives every copy.
class ShaderProgram final
{
public:
    /// @brief Creates an empty handle.
    ShaderProgram() = default;

    /// @brief Reports whether the handle owns a program.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class ResourceManager;
    friend class core::vulkan::renderer::MeshPipelineCache;
    friend class core::vulkan::renderer::ApplicationShaderBindings;
    struct State;
    struct Identity;

    [[nodiscard]] static auto create(const vk::raii::Device& device, const ProgramDesc& description)
        -> Result<ShaderProgram>;
    [[nodiscard]] auto snapshot() const -> std::shared_ptr<const State>;

    explicit ShaderProgram(std::shared_ptr<const State> state);

    std::shared_ptr<Identity> _identity;
};
}

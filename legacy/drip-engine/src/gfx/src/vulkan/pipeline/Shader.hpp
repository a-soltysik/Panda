#pragma once

// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <cstdint>
#include <optional>
#include <span>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

namespace drip::gfx
{

class Shader
{
public:
    enum class Type : uint8_t
    {
        Vertex,
        TessellationControl,
        TessellationEvaluation,
        Geometry,
        Fragment,
        Compute
    };

    Shader(const vk::ShaderModule& shaderModule, Type shaderType, const vk::Device& device) noexcept;
    Shader(const Shader&) = delete;
    Shader(Shader&&) = delete;
    auto operator=(const Shader&) -> Shader& = delete;
    auto operator=(Shader&&) -> Shader& = delete;
    ~Shader() noexcept;

    [[nodiscard]] static auto create(const vk::Device& device, std::span<const uint32_t> buffer, Type type)
        -> std::optional<Shader>;

    [[nodiscard]] static constexpr auto getEntryPointName() -> const char*
    {
        return "main";
    }

    const vk::ShaderModule module;
    const Type type;

private:
    const vk::Device& _device;
};

}

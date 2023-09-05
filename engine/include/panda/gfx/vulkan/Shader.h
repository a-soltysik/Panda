#pragma once

#include <filesystem>
#include <optional>
#include <span>

#include "Device.h"

namespace panda::gfx::vulkan
{

class Shader
{
public:
    enum class Type
    {
        Vertex,
        TessellationControl,
        TessellationEvaluation,
        Geometry,
        Fragment,
        Compute
    };

    Shader(const vk::ShaderModule& shaderModule, Type shaderType, const vk::Device& device) noexcept;
    PD_DELETE_ALL(Shader);
    ~Shader() noexcept;

    [[nodiscard]] static auto createFromFile(const vk::Device& device, const std::filesystem::path& path)
        -> std::optional<Shader>;
    [[nodiscard]] static auto createFromFile(const vk::Device& device, const std::filesystem::path& path, Type type)
        -> std::optional<Shader>;
    [[nodiscard]] static auto createFromRawData(const vk::Device& device,
                                                const std::vector<uint32_t>& buffer,
                                                Type type) -> std::optional<Shader>;

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

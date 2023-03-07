#pragma once

#include <filesystem>
#include <optional>
#include <span>

namespace panda::gfx
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

    [[nodiscard]] static auto createFromFile(const vk::Device& device, const std::filesystem::path& path)
        -> std::optional<Shader>;
    [[nodiscard]] static auto createFromFile(const vk::Device& device, const std::filesystem::path& path, Type type)
        -> std::optional<Shader>;
    [[nodiscard]] static auto createFromRawData(const vk::Device& device,
                                                const std::vector<uint32_t>& buffer,
                                                Type type) -> std::optional<Shader>;

    const vk::ShaderModule module;
    const Type type;

private:
    explicit Shader(const vk::Device& shaderDevice, const vk::ShaderModule& shaderModule, Type shaderType);

    const vk::Device& device;
};

}

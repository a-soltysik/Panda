#include "Shader.h"

#include <fstream>
#include <unordered_map>

#include "utils/format/api/vulkan/ResultFormatter.h"

namespace panda::gfx
{

auto Shader::createFromFile(const vk::Device& device, const std::filesystem::path& path) -> std::optional<Shader>
{
    using namespace std::string_view_literals;
    static const std::unordered_map<std::string_view, Type> extensions = {
        {".vert"sv,                 Type::Vertex},
        {".tesc"sv,    Type::TessellationControl},
        {".tese"sv, Type::TessellationEvaluation},
        {".geom"sv,               Type::Geometry},
        {".frag"sv,               Type::Fragment},
        {".comp"sv,                Type::Compute}
    };

    const auto pathStr = path.string();
    const auto shaderExtension = pathStr.substr(pathStr.find('.'), pathStr.size() - pathStr.rfind('.') + 1);

    const auto it = extensions.find(shaderExtension);
    if (it == extensions.cend())
    {
        log::Warning("File extension: {} is not supported (filename: {})", shaderExtension, pathStr);
        return {};
    }
    return createFromFile(device, path, it->second);
}

auto Shader::createFromFile(const vk::Device& device, const std::filesystem::path& path, Type type)
    -> std::optional<Shader>
{
    auto fin = std::ifstream(path, std::ios::ate | std::ios::binary);

    if (!fin.is_open())
    {
        log::Warning("File {} cannot be opened", path.string());
        return {};
    }

    const auto fileSize = fin.tellg();
    auto buffer = std::vector<uint32_t>(fileSize / sizeof(uint32_t));

    fin.seekg(0);
    fin.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    return createFromRawData(device, buffer, type);
}

auto Shader::createFromRawData(const vk::Device& device, const std::vector<uint32_t>& buffer, Type type)
    -> std::optional<Shader>
{
    const auto createInfo = vk::ShaderModuleCreateInfo {{}, buffer};
    const auto shaderModuleResult = device.createShaderModule(createInfo);
    if (shaderModuleResult.result == vk::Result::eSuccess)
    {
        return Shader {device, shaderModuleResult.value, type};
    }

    log::Warning("Creating shader module didn't succeed: {}", shaderModuleResult.result);
    return {};
}

Shader::Shader(const vk::Device& shaderDevice, const vk::ShaderModule& shaderModule, Type shaderType)
    : module {shaderModule},
      type {shaderType},
      device {shaderDevice}
{
}

}
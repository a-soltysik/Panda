// clang-format off
#include <drip/common/utils/Assert.hpp> // NOLINT(misc-include-cleaner)
// clang-format on

#include "Shader.hpp"

#include <cstdint>
#include <drip/common/log/LogMessageBuilder.hpp>
#include <optional>
#include <span>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "drip/gfx/utils/format/ResultFormatter.hpp"  //NOLINT(misc-include-cleaner)

namespace drip::gfx
{

auto Shader::create(const vk::Device& device, std::span<const uint32_t> buffer, Type type) -> std::optional<Shader>
{
    const auto createInfo = vk::ShaderModuleCreateInfo {.codeSize = buffer.size_bytes(), .pCode = buffer.data()};
    const auto shaderModuleResult = device.createShaderModule(createInfo);
    if (shaderModuleResult.result == vk::Result::eSuccess)
    {
        return std::make_optional<Shader>(shaderModuleResult.value, type, device);
    }

    common::log::Warning("Creating shader module didn't succeed: {}", shaderModuleResult.result);
    return {};
}

Shader::~Shader() noexcept
{
    common::log::Info("Destroying shader [{}]", static_cast<void*>(module));
    _device.destroy(module);
}

Shader::Shader(const vk::ShaderModule& shaderModule, Type shaderType, const vk::Device& device) noexcept
    : module {shaderModule},
      type {shaderType},
      _device {device}
{
    common::log::Info("Created shader [{}]", static_cast<void*>(module));
}

}

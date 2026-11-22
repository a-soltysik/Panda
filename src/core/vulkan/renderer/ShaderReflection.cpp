#include "renderer/ShaderReflection.hpp"

#include <spirv_reflect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <panda/base/Error.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace panda::core::vulkan::renderer
{
namespace
{
constexpr auto spirvMagic = std::uint32_t {0x07230203U};
constexpr auto spirvHeaderWordCount = std::size_t {5};

class ShaderModule final
{
public:
    explicit ShaderModule(SpvReflectShaderModule& module)
        : _module {module}
    {
    }

    ShaderModule(const ShaderModule&) = delete;
    auto operator=(const ShaderModule&) -> ShaderModule& = delete;
    ShaderModule(ShaderModule&&) = delete;
    auto operator=(ShaderModule&&) -> ShaderModule& = delete;

    ~ShaderModule()
    {
        spvReflectDestroyShaderModule(&_module);
    }

private:
    SpvReflectShaderModule& _module;
};

auto shaderError(std::string_view label, std::string_view reason) -> Error
{
    return makeError(ErrorCode::InvalidArgument,
                     std::string {"Shader '"} + std::string {label} + "': " + std::string {reason});
}

auto reflectError(std::string_view label, SpvReflectResult result) -> Error
{
    const auto code =
        result == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED ? ErrorCode::ResourceExhausted : ErrorCode::InvalidArgument;
    return makeError(code,
                     std::string {"SPIRV-Reflect could not inspect shader '"} + std::string {label} + "'",
                     NativeError {.api = "SPIRV-Reflect", .code = static_cast<std::int64_t>(result)});
}

auto matchesStage(SpvReflectShaderStageFlagBits actual, ShaderStage expected) -> bool
{
    switch (expected)
    {
    case ShaderStage::Vertex:
        return actual == SPV_REFLECT_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment:
        return actual == SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT;
    }
    return false;
}

auto descriptorKind(SpvReflectDescriptorType type) -> ShaderDescriptorKind
{
    switch (type)
    {
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return ShaderDescriptorKind::UniformBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return ShaderDescriptorKind::StorageBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return ShaderDescriptorKind::CombinedImageSampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return ShaderDescriptorKind::Other;
    }
    return ShaderDescriptorKind::Other;
}

auto reflectMembers(const SpvReflectBlockVariable& block) -> std::vector<ShaderBlockMember>
{
    auto members = std::vector<ShaderBlockMember> {};
    members.reserve(block.member_count);
    for (const auto& member : std::span {block.members, block.member_count})
    {
        members.push_back(
            {.name = member.name != nullptr ? member.name : "", .offset = member.offset, .size = member.size});
    }
    return members;
}

auto reflectDescriptor(const SpvReflectDescriptorBinding& binding) -> ShaderDescriptor
{
    return {.set = binding.set,
            .binding = binding.binding,
            .count = binding.count,
            .kind = descriptorKind(binding.descriptor_type),
            .blockSize = binding.block.size,
            .members = reflectMembers(binding.block)};
}

auto reflectPushConstant(const SpvReflectBlockVariable& block) -> ShaderPushConstantBlock
{
    return {.offset = block.offset, .size = block.size, .members = reflectMembers(block)};
}

template <typename Element, typename Enumerate>
auto enumerateReflection(std::string_view label, Enumerate enumerate) -> Result<std::vector<Element*>>
{
    auto count = std::uint32_t {};
    auto result = enumerate(&count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        return std::unexpected {reflectError(label, result)};
    }

    auto elements = std::vector<Element*>(count);
    if (count > 0)
    {
        result = enumerate(&count, elements.data());
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            return std::unexpected {reflectError(label, result)};
        }
        elements.resize(count);
    }
    return elements;
}

auto reflectDescriptors(const SpvReflectShaderModule& module, std::string_view label)
    -> Result<std::vector<ShaderDescriptor>>
{
    auto bindings = enumerateReflection<SpvReflectDescriptorBinding>(
        label,
        [&module](std::uint32_t* count, SpvReflectDescriptorBinding** output) {
            return spvReflectEnumerateEntryPointDescriptorBindings(&module, "main", count, output);
        });
    if (!bindings)
    {
        return std::unexpected {bindings.error()};
    }

    auto descriptors = std::vector<ShaderDescriptor> {};
    descriptors.reserve(bindings->size());
    std::ranges::transform(*bindings, std::back_inserter(descriptors), [](const auto* binding) {
        return reflectDescriptor(*binding);
    });
    return descriptors;
}

auto reflectPushConstants(const SpvReflectShaderModule& module, std::string_view label)
    -> Result<std::vector<ShaderPushConstantBlock>>
{
    auto blocks = enumerateReflection<SpvReflectBlockVariable>(
        label,
        [&module](std::uint32_t* count, SpvReflectBlockVariable** output) {
            return spvReflectEnumerateEntryPointPushConstantBlocks(&module, "main", count, output);
        });
    if (!blocks)
    {
        return std::unexpected {blocks.error()};
    }

    auto pushConstants = std::vector<ShaderPushConstantBlock> {};
    pushConstants.reserve(blocks->size());
    std::ranges::transform(*blocks, std::back_inserter(pushConstants), [](const auto* block) {
        return reflectPushConstant(*block);
    });
    return pushConstants;
}

auto reflectModule(const SpvReflectShaderModule& module, ShaderStage expectedStage, std::string_view label)
    -> Result<ShaderReflection>
{
    constexpr auto entryPointName = "main";
    const auto* entryPoint = spvReflectGetEntryPoint(&module, entryPointName);
    if (entryPoint == nullptr)
    {
        return std::unexpected {shaderError(label, "must expose the 'main' entry point")};
    }
    if (!matchesStage(entryPoint->shader_stage, expectedStage))
    {
        return std::unexpected {shaderError(label, "does not match its declared shader stage")};
    }

    auto descriptors = reflectDescriptors(module, label);
    if (!descriptors)
    {
        return std::unexpected {descriptors.error()};
    }
    auto pushConstants = reflectPushConstants(module, label);
    if (!pushConstants)
    {
        return std::unexpected {pushConstants.error()};
    }
    return ShaderReflection {.stage = expectedStage,
                             .descriptors = std::move(*descriptors),
                             .pushConstantBlocks = std::move(*pushConstants)};
}
}

auto reflectShader(std::span<const std::uint32_t> words, ShaderStage expectedStage, std::string_view diagnosticLabel)
    -> Result<ShaderReflection>
{
    if (words.size() < spirvHeaderWordCount || words.front() != spirvMagic)
    {
        return std::unexpected {shaderError(diagnosticLabel, "has an invalid SPIR-V header")};
    }

    SpvReflectShaderModule module;
    const auto result = spvReflectCreateShaderModule(words.size_bytes(), words.data(), &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        return std::unexpected {reflectError(diagnosticLabel, result)};
    }

    const auto moduleGuard = ShaderModule {module};
    return reflectModule(module, expectedStage, diagnosticLabel);
}
}

#include "renderer/ShaderReflection.hpp"

#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <panda/base/Error.hpp>
#include <ranges>
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
    return makeError(ErrorCode::InvalidArgument, std::format("Shader '{}': {}", label, reason));
}

auto reflectError(std::string_view label, SpvReflectResult result) -> Error
{
    const auto code =
        result == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED ? ErrorCode::ResourceExhausted : ErrorCode::InvalidArgument;
    return makeError(code,
                     std::format("SPIRV-Reflect could not inspect shader '{}'", label),
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

// NOLINTNEXTLINE(misc-no-recursion) SPIR-V block members form a finite tree.
auto reflectMembers(const SpvReflectBlockVariable& block) -> std::vector<ShaderBlockMember>
{
    auto members = std::vector<ShaderBlockMember> {};
    members.reserve(block.member_count);
    for (const auto& member : std::span {block.members, block.member_count})
    {
        auto arrayStride = member.array.stride;
        if (arrayStride == 0 && member.type_description != nullptr)
        {
            arrayStride = member.type_description->traits.array.stride;
        }
        members.push_back({.name = member.name != nullptr ? member.name : "",
                           .offset = member.offset,
                           .size = member.size,
                           .arrayStride = arrayStride,
                           .members = reflectMembers(member)});
    }
    return members;
}

auto reflectDescriptor(const SpvReflectDescriptorBinding& binding) -> ShaderDescriptor
{
    return {.name = binding.name != nullptr ? binding.name : "",
            .set = binding.set,
            .binding = binding.binding,
            .count = binding.count,
            .kind = descriptorKind(binding.descriptor_type),
            .arrayDimensions =
                std::vector<std::uint32_t>(std::span {binding.array.dims}.first(binding.array.dims_count).begin(),
                                           std::span {binding.array.dims}.first(binding.array.dims_count).end()),
            .blockSize = binding.block.size,
            .members = reflectMembers(binding.block)};
}

auto reflectPushConstant(const SpvReflectBlockVariable& block, std::string_view label)
    -> Result<ShaderPushConstantBlock>
{
    if (block.size < block.offset)
    {
        return std::unexpected {shaderError(label, "has an invalid push constant range")};
    }
    return ShaderPushConstantBlock {.offset = block.offset,
                                    .size = block.size - block.offset,
                                    .members = reflectMembers(block)};
}

auto interfaceFormat(SpvReflectFormat format) -> std::optional<ShaderInterfaceFormat>
{
    switch (std::to_underlying(format))
    {
    case std::to_underlying(SPV_REFLECT_FORMAT_R32_SFLOAT):
        return ShaderInterfaceFormat::Float;
    case std::to_underlying(SPV_REFLECT_FORMAT_R32_SINT):
        return ShaderInterfaceFormat::SignedInteger;
    case std::to_underlying(SPV_REFLECT_FORMAT_R32_UINT):
        return ShaderInterfaceFormat::UnsignedInteger;
    case std::to_underlying(SPV_REFLECT_FORMAT_R32G32_SFLOAT):
        return ShaderInterfaceFormat::Float2;
    case std::to_underlying(SPV_REFLECT_FORMAT_R32G32B32_SFLOAT):
        return ShaderInterfaceFormat::Float3;
    case std::to_underlying(SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT):
        return ShaderInterfaceFormat::Float4;
    default:
        return std::nullopt;
    }
}

template <typename Element, typename Enumerate>
auto enumerateReflection(std::string_view label, Enumerate enumerate) -> Result<std::vector<Element*>>;

auto copyInterfaceVariable(const SpvReflectInterfaceVariable& variable, std::string_view label)
    -> Result<ShaderInterfaceVariable>
{
    if (variable.location == std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {shaderError(label, "has an interface variable without a location")};
    }
    if (variable.component != 0 && variable.component != std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {shaderError(label, "uses an unsupported nonzero interface component")};
    }
    const auto format = interfaceFormat(variable.format);
    if (!format)
    {
        return std::unexpected {shaderError(label, "uses an unsupported interface format")};
    }
    return ShaderInterfaceVariable {.name = variable.name != nullptr ? variable.name : "",
                                    .location = variable.location,
                                    .format = *format};
}

auto copyInterfaceVariables(std::span<SpvReflectInterfaceVariable* const> variables, std::string_view label)
    -> Result<std::vector<ShaderInterfaceVariable>>
{
    auto result = std::vector<ShaderInterfaceVariable> {};
    result.reserve(variables.size());
    for (const auto* variable : variables)
    {
        if (variable->built_in >= 0 ||
            (variable->decoration_flags & static_cast<std::uint32_t>(SPV_REFLECT_DECORATION_BUILT_IN)) != 0U)
        {
            continue;
        }
        auto reflected = copyInterfaceVariable(*variable, label);
        if (!reflected)
        {
            return std::unexpected {reflected.error()};
        }
        result.push_back(std::move(*reflected));
    }
    return result;
}

auto reflectInterfaceVariables(const SpvReflectShaderModule& module, std::string_view label, bool inputs)
    -> Result<std::vector<ShaderInterfaceVariable>>
{
    auto variables = enumerateReflection<SpvReflectInterfaceVariable>(
        label,
        [&module, inputs](std::uint32_t* count, SpvReflectInterfaceVariable** output) {
            constexpr auto entryPointName = "main";
            return inputs ? spvReflectEnumerateEntryPointInputVariables(&module, entryPointName, count, output)
                          : spvReflectEnumerateEntryPointOutputVariables(&module, entryPointName, count, output);
        });
    return std::move(variables).and_then([&](const auto& entries) {
        return copyInterfaceVariables(entries, label);
    });
}

auto sameDescriptorBinding(const ShaderProgramDescriptor& current, const ShaderDescriptor& next) -> bool
{
    return current.count == next.count && current.kind == next.kind;
}

auto hasDuplicateLocations(std::span<const ShaderInterfaceVariable> variables) -> bool
{
    auto locations = std::vector<std::uint32_t> {};
    locations.reserve(variables.size());
    std::ranges::transform(variables, std::back_inserter(locations), &ShaderInterfaceVariable::location);
    std::ranges::sort(locations);
    return std::ranges::adjacent_find(locations) != locations.end();
}

auto stagesOverlap(std::span<const ShaderStage> first, ShaderStage second) -> bool
{
    return std::ranges::find(first, second) != first.end();
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
    for (const auto* block : *blocks)
    {
        auto reflected = reflectPushConstant(*block, label);
        if (!reflected)
        {
            return std::unexpected {reflected.error()};
        }
        pushConstants.push_back(std::move(*reflected));
    }
    return pushConstants;
}

auto reflectSpecializationConstants(const SpvReflectShaderModule& module, std::string_view label)
    -> Result<std::vector<ShaderSpecializationConstant>>
{
    auto constants = enumerateReflection<SpvReflectSpecializationConstant>(
        label,
        [&module](std::uint32_t* count, SpvReflectSpecializationConstant** output) {
            return spvReflectEnumerateSpecializationConstants(&module, count, output);
        });
    if (!constants)
    {
        return std::unexpected {constants.error()};
    }

    auto result = std::vector<ShaderSpecializationConstant> {};
    result.reserve(constants->size());
    std::ranges::transform(*constants, std::back_inserter(result), [](const auto* constant) {
        return ShaderSpecializationConstant {.name = constant->name != nullptr ? constant->name : "",
                                             .id = constant->constant_id};
    });
    return result;
}

auto readModuleResources(const SpvReflectShaderModule& module, std::string_view label, ShaderReflection& reflection)
    -> Result<void>
{
    return reflectDescriptors(module, label)
        .transform([&](auto value) {
            reflection.descriptors = std::move(value);
        })
        .and_then([&] {
            return reflectPushConstants(module, label);
        })
        .transform([&](auto value) {
            reflection.pushConstantBlocks = std::move(value);
        });
}

auto readModuleInterface(const SpvReflectShaderModule& module, ShaderStage expectedStage, std::string_view label)
    -> Result<ShaderReflection>
{
    auto reflection = ShaderReflection {};
    reflection.stage = expectedStage;
    return readModuleResources(module, label, reflection)
        .and_then([&] {
            return reflectInterfaceVariables(module, label, true);
        })
        .transform([&](auto value) {
            reflection.inputs = std::move(value);
        })
        .and_then([&] {
            return reflectInterfaceVariables(module, label, false);
        })
        .transform([&](auto value) {
            reflection.outputs = std::move(value);
        })
        .and_then([&] {
            return reflectSpecializationConstants(module, label);
        })
        .transform([&](auto value) {
            reflection.specializationConstants = std::move(value);
            return std::move(reflection);
        });
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

    return readModuleInterface(module, expectedStage, label);
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

auto reflectGraphicsProgram(std::span<const std::uint32_t> vertexWords,
                            std::optional<std::span<const std::uint32_t>> fragmentWords,
                            std::string_view diagnosticLabel) -> Result<ShaderProgramLayout>
{
    auto shaders = std::vector<ShaderReflection> {};
    auto vertex = reflectShader(vertexWords, ShaderStage::Vertex, std::format("{} vertex", diagnosticLabel));
    if (!vertex)
    {
        return std::unexpected {vertex.error()};
    }
    shaders.push_back(std::move(*vertex));
    if (fragmentWords)
    {
        auto fragment =
            reflectShader(*fragmentWords, ShaderStage::Fragment, std::format("{} fragment", diagnosticLabel));
        if (!fragment)
        {
            return std::unexpected {fragment.error()};
        }
        shaders.push_back(std::move(*fragment));
    }
    return buildShaderProgramLayout(shaders, diagnosticLabel);
}

namespace
{
using ProgramDescriptors = std::map<std::pair<std::uint32_t, std::uint32_t>, ShaderProgramDescriptor>;
using ProgramPushConstants = std::map<std::pair<std::uint32_t, std::uint32_t>, ShaderProgramPushConstantBlock>;

auto validateStageLink(const std::vector<ShaderInterfaceVariable>& vertexOutputs,
                       const ShaderReflection& fragment,
                       std::string_view diagnosticLabel) -> Result<void>
{
    if (hasDuplicateLocations(fragment.inputs))
    {
        return std::unexpected {shaderError(diagnosticLabel, "has duplicate fragment input locations")};
    }
    for (const auto& input : fragment.inputs)
    {
        const auto output = std::ranges::find(vertexOutputs, input.location, &ShaderInterfaceVariable::location);
        if (output == vertexOutputs.end() || output->format != input.format)
        {
            return std::unexpected {shaderError(
                diagnosticLabel,
                std::format("has incompatible vertex and fragment interfaces at location {}", input.location))};
        }
    }
    return {};
}

auto addSpecialization(std::vector<ShaderProgramSpecializationConstant>& specializationConstants,
                       const ShaderSpecializationConstant& constant,
                       ShaderStage stage,
                       std::string_view diagnosticLabel) -> Result<void>
{
    const auto duplicate = std::ranges::find_if(specializationConstants, [&](const auto& previous) {
        return previous.stage == stage && (previous.id == constant.id || previous.name == constant.name);
    });
    if (duplicate != specializationConstants.end())
    {
        return std::unexpected {shaderError(diagnosticLabel, "has duplicate specialization constants")};
    }
    specializationConstants.push_back({.name = constant.name, .id = constant.id, .stage = stage});
    return {};
}

auto addDescriptor(ProgramDescriptors& descriptors,
                   const ShaderDescriptor& descriptor,
                   ShaderStage stage,
                   std::string_view diagnosticLabel) -> Result<void>
{
    if (descriptor.count != 1 || !descriptor.arrayDimensions.empty() || descriptor.kind == ShaderDescriptorKind::Other)
    {
        return std::unexpected {shaderError(
            diagnosticLabel,
            std::format("uses an unsupported descriptor at set {}, binding {}", descriptor.set, descriptor.binding))};
    }
    const auto key = std::pair {descriptor.set, descriptor.binding};
    const auto [entry, inserted] = descriptors.try_emplace(key,
                                                           ShaderProgramDescriptor {.set = descriptor.set,
                                                                                    .binding = descriptor.binding,
                                                                                    .count = descriptor.count,
                                                                                    .kind = descriptor.kind,
                                                                                    .stageLayouts = {},
                                                                                    .names = {}});
    if (!inserted && !sameDescriptorBinding(entry->second, descriptor))
    {
        return std::unexpected {shaderError(
            diagnosticLabel,
            std::format("has conflicting descriptors at set {}, binding {}", descriptor.set, descriptor.binding))};
    }
    entry->second.stageLayouts.push_back(
        {.stage = stage, .blockSize = descriptor.blockSize, .members = descriptor.members});
    if (!descriptor.name.empty() &&
        std::ranges::find(entry->second.names, descriptor.name) == entry->second.names.end())
    {
        entry->second.names.push_back(descriptor.name);
    }
    return {};
}

auto addPushConstant(ProgramPushConstants& pushConstants,
                     const ShaderPushConstantBlock& block,
                     ShaderStage stage,
                     std::string_view diagnosticLabel) -> Result<void>
{
    if (block.size == 0 ||
        static_cast<std::uint64_t>(block.offset) + block.size > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {shaderError(diagnosticLabel, "has an invalid push constant range")};
    }
    const auto key = std::pair {block.offset, block.size};
    const auto sameStage = std::ranges::find_if(pushConstants, [stage](const auto& entry) {
        return stagesOverlap(entry.second.stages, stage);
    });
    if (sameStage != pushConstants.end())
    {
        return std::unexpected {shaderError(diagnosticLabel, "declares more than one push constant range per stage")};
    }
    const auto [entry, inserted] =
        pushConstants.try_emplace(key, ShaderProgramPushConstantBlock {.block = block, .stages = {}});
    if (!inserted && entry->second.block.members != block.members)
    {
        return std::unexpected {shaderError(diagnosticLabel, "has conflicting push constant layouts")};
    }
    entry->second.stages.push_back(stage);
    return {};
}

struct ProgramInterface
{
    ProgramDescriptors descriptors;
    ProgramPushConstants pushConstants;
    std::vector<ShaderProgramSpecializationConstant> specializationConstants;
};

template <typename Entries, typename Add>
auto addStageEntries(const Entries& entries, Add add) -> Result<void>
{
    for (const auto& entry : entries)
    {
        if (auto added = add(entry); !added)
        {
            return added;
        }
    }
    return {};
}

auto addStage(ProgramInterface& program, const ShaderReflection& shader, std::string_view label) -> Result<void>
{
    return addStageEntries(shader.specializationConstants,
                           [&](const auto& constant) {
                               return addSpecialization(program.specializationConstants, constant, shader.stage, label);
                           })
        .and_then([&] {
            return addStageEntries(shader.descriptors, [&](const auto& descriptor) {
                return addDescriptor(program.descriptors, descriptor, shader.stage, label);
            });
        })
        .and_then([&] {
            return addStageEntries(shader.pushConstantBlocks, [&](const auto& block) {
                return addPushConstant(program.pushConstants, block, shader.stage, label);
            });
        });
}

template <typename Map>
auto takeValues(Map& map) -> std::vector<typename Map::mapped_type>
{
    auto values = std::vector<typename Map::mapped_type> {};
    values.reserve(map.size());
    std::ranges::transform(map, std::back_inserter(values), [](auto& entry) {
        return std::move(entry.second);
    });
    return values;
}

auto mergeStages(std::span<const ShaderReflection> shaders,
                 std::span<const ShaderInterfaceVariable> vertexInputs,
                 std::string_view label) -> Result<ShaderProgramLayout>
{
    auto program = ProgramInterface {.descriptors = {}, .pushConstants = {}, .specializationConstants = {}};
    for (const auto& shader : shaders)
    {
        if (auto added = addStage(program, shader, label); !added)
        {
            return std::unexpected {std::move(added.error())};
        }
    }
    auto inputs = std::vector(vertexInputs.begin(), vertexInputs.end());
    std::ranges::sort(inputs, {}, &ShaderInterfaceVariable::location);
    return ShaderProgramLayout {.descriptors = takeValues(program.descriptors),
                                .pushConstantBlocks = takeValues(program.pushConstants),
                                .vertexInputs = std::move(inputs),
                                .specializationConstants = std::move(program.specializationConstants)};
}

auto vertexStage(std::span<const ShaderReflection> shaders, std::string_view diagnosticLabel)
    -> Result<std::span<const ShaderReflection>::iterator>
{
    if (shaders.empty())
    {
        return std::unexpected {shaderError(diagnosticLabel, "has no shader stages")};
    }

    const auto vertex = std::ranges::find(shaders, ShaderStage::Vertex, &ShaderReflection::stage);
    if (vertex == shaders.end())
    {
        return std::unexpected {shaderError(diagnosticLabel, "must include a vertex stage")};
    }
    if (std::ranges::count(shaders, ShaderStage::Vertex, &ShaderReflection::stage) > 1 ||
        std::ranges::count(shaders, ShaderStage::Fragment, &ShaderReflection::stage) > 1)
    {
        return std::unexpected {shaderError(diagnosticLabel, "contains a duplicate shader stage")};
    }
    if (hasDuplicateLocations(vertex->inputs) || hasDuplicateLocations(vertex->outputs))
    {
        return std::unexpected {shaderError(diagnosticLabel, "has duplicate vertex interface locations")};
    }

    return vertex;
}
}

auto buildShaderProgramLayout(std::span<const ShaderReflection> shaders, std::string_view diagnosticLabel)
    -> Result<ShaderProgramLayout>
{
    auto vertex = vertexStage(shaders, diagnosticLabel);
    if (!vertex)
    {
        return std::unexpected {std::move(vertex.error())};
    }
    const auto fragment = std::ranges::find(shaders, ShaderStage::Fragment, &ShaderReflection::stage);
    if (fragment != shaders.end())
    {
        if (auto linked = validateStageLink((*vertex)->outputs, *fragment, diagnosticLabel); !linked)
        {
            return std::unexpected {std::move(linked.error())};
        }
    }
    return mergeStages(shaders, (*vertex)->inputs, diagnosticLabel);
}

auto specializationConstantId(const ShaderProgramLayout& program,
                              ShaderStage stage,
                              std::string_view name,
                              std::string_view diagnosticLabel) -> Result<std::uint32_t>
{
    const auto constant = std::ranges::find_if(program.specializationConstants, [&](const auto& value) {
        return value.stage == stage && value.name == name;
    });
    if (constant == program.specializationConstants.end())
    {
        return std::unexpected {
            shaderError(diagnosticLabel, std::format("does not declare specialization constant '{}'", name))};
    }
    return constant->id;
}

namespace
{
// NOLINTNEXTLINE(misc-no-recursion) Validation follows the finite reflected block tree.
auto validateShaderBlockMembersRecursive(std::span<const ShaderBlockMember> shaderMembers,
                                         std::span<const ShaderBlockMember> hostMembers,
                                         bool requireComplete) -> std::optional<std::string>
{
    if (requireComplete && shaderMembers.size() != hostMembers.size())
    {
        return std::string {"has an incomplete shader block"};
    }
    for (const auto& member : shaderMembers)
    {
        const auto expected = std::ranges::find(hostMembers, member.name, &ShaderBlockMember::name);
        if (expected == hostMembers.end() || expected->offset != member.offset ||
            expected->arrayStride != member.arrayStride || (member.arrayStride == 0 && expected->size != member.size))
        {
            return std::format("has an incompatible block member '{}'", member.name);
        }
        if (auto nested = validateShaderBlockMembersRecursive(member.members,
                                                              expected->members,
                                                              requireComplete || member.arrayStride != 0);
            nested)
        {
            return nested;
        }
    }
    return std::nullopt;
}

auto validateShaderProgramDescriptor(const ShaderProgramDescriptor& descriptor,
                                     std::span<const ShaderDescriptor> expectedDescriptors,
                                     std::string_view diagnosticLabel) -> Result<void>
{
    if (descriptor.names.empty())
    {
        return std::unexpected {shaderError(diagnosticLabel, "has an unnamed descriptor")};
    }
    for (const auto& name : descriptor.names)
    {
        const auto expected = std::ranges::find(expectedDescriptors, name, &ShaderDescriptor::name);
        if (expected == expectedDescriptors.end() || expected->set != descriptor.set ||
            expected->binding != descriptor.binding || expected->count != descriptor.count ||
            expected->kind != descriptor.kind)
        {
            return std::unexpected {
                shaderError(diagnosticLabel, std::format("uses descriptor '{}' outside the shared layout", name))};
        }
    }
    return {};
}
}

auto validateShaderBlockMembers(std::span<const ShaderBlockMember> shaderMembers,
                                std::span<const ShaderBlockMember> hostMembers,
                                std::string_view diagnosticLabel,
                                bool requireComplete) -> Result<void>
{
    if (auto reason = validateShaderBlockMembersRecursive(shaderMembers, hostMembers, requireComplete); reason)
    {
        return std::unexpected {shaderError(diagnosticLabel, *reason)};
    }
    return {};
}

auto validateShaderProgramDescriptors(const ShaderProgramLayout& program,
                                      std::span<const ShaderDescriptor> expectedDescriptors,
                                      std::string_view diagnosticLabel) -> Result<void>
{
    for (const auto& descriptor : program.descriptors)
    {
        if (auto valid = validateShaderProgramDescriptor(descriptor, expectedDescriptors, diagnosticLabel); !valid)
        {
            return valid;
        }
    }
    return {};
}
}

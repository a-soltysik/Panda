// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameDescriptors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <glm/ext/matrix_float3x4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/vulkan/Error.hpp>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "BuiltinShaders.hpp"
#include "VulkanError.hpp"
#include "renderer/DescriptorSets.hpp"
#include "renderer/FrameBuffers.hpp"
#include "renderer/MaterialTextureSlots.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderReflection.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;
using detail::createVulkanSampler;

FrameDescriptors::FrameDescriptors(const vk::raii::Device& device)
    : _device {device}
{
}

namespace
{
constexpr auto frameUniformName = std::string_view {"frameData"};
constexpr auto shadowMapName = std::string_view {"shadowMap"};
constexpr auto materialDataName = std::string_view {"materialData"};
constexpr auto lightDataName = std::string_view {"lightData"};
constexpr auto shadowDataName = std::string_view {"shadowData"};
constexpr auto shadowViewName = std::string_view {"shadowViewData"};
constexpr auto diffuseEnvironmentName = std::string_view {"diffuseIrradianceMap"};
constexpr auto specularEnvironmentName = std::string_view {"specularRadianceMap"};
constexpr auto skyboxMapName = std::string_view {"skyboxMap"};

struct BuiltinShaderProgramWords
{
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
    std::string_view name;
};

constexpr auto builtinShaderPrograms = std::array {
    BuiltinShaderProgramWords {.vertex = shaders::meshLitColorNormalVertex,
                               .fragment = shaders::meshNormalEnvironmentFragment,
                               .name = "mesh"                                                                                          },
    BuiltinShaderProgramWords {.vertex = shaders::shadowVertexAlphaTextureVertex,
                               .fragment = shaders::shadowTextureFragment,
                               .name = "shadow"                                                                                        },
    BuiltinShaderProgramWords {.vertex = shaders::skyboxVertex,                   .fragment = shaders::skyboxFragment, .name = "skybox"}
};

struct ShaderDescriptorDefinition
{
    std::string_view name;
    ShaderDescriptorKind kind;
};

constexpr auto frameDescriptorDefinitions = std::array {
    ShaderDescriptorDefinition {.name = frameUniformName,        .kind = ShaderDescriptorKind::UniformBuffer       },
    ShaderDescriptorDefinition {.name = shadowMapName,           .kind = ShaderDescriptorKind::CombinedImageSampler},
    ShaderDescriptorDefinition {.name = materialDataName,        .kind = ShaderDescriptorKind::StorageBuffer       },
    ShaderDescriptorDefinition {.name = lightDataName,           .kind = ShaderDescriptorKind::StorageBuffer       },
    ShaderDescriptorDefinition {.name = shadowDataName,          .kind = ShaderDescriptorKind::StorageBuffer       },
    ShaderDescriptorDefinition {.name = shadowViewName,          .kind = ShaderDescriptorKind::UniformBuffer       },
    ShaderDescriptorDefinition {.name = diffuseEnvironmentName,  .kind = ShaderDescriptorKind::CombinedImageSampler},
    ShaderDescriptorDefinition {.name = specularEnvironmentName, .kind = ShaderDescriptorKind::CombinedImageSampler},
    ShaderDescriptorDefinition {.name = skyboxMapName,           .kind = ShaderDescriptorKind::CombinedImageSampler}
};

struct ReflectedDescriptorInterface
{
    std::uint32_t set {};
    std::uint32_t binding {};
    std::uint32_t count {};
    ShaderDescriptorKind kind {ShaderDescriptorKind::Other};
    vk::ShaderStageFlags stages;
};

struct BuiltinShaderBindings
{
    std::map<std::string, ReflectedDescriptorBinding, std::less<>> descriptors;
    std::vector<ShaderDescriptor> shaderDescriptorSchema;
    vk::PushConstantRange pushConstantRange;
    std::uint32_t frameSet {};
    std::uint32_t textureSet {};
};

auto hostBlockMember(std::string_view name, std::size_t offset, std::size_t size) -> ShaderBlockMember
{
    return {.name = std::string {name},
            .offset = static_cast<std::uint32_t>(offset),
            .size = static_cast<std::uint32_t>(size),
            .members = {}};
}

auto hostBlockArrayMember(std::string_view name, std::size_t arrayStride, std::vector<ShaderBlockMember> members)
    -> ShaderBlockMember
{
    return {.name = std::string {name},
            .arrayStride = static_cast<std::uint32_t>(arrayStride),
            .members = std::move(members)};
}

auto frameUniformMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockMember("viewProjection", offsetof(FrameUniform, viewProjection), sizeof(glm::mat4)),
            hostBlockMember("cameraPosition", offsetof(FrameUniform, cameraPosition), sizeof(glm::vec4)),
            hostBlockMember("ambient", offsetof(FrameUniform, ambient), sizeof(glm::vec4)),
            hostBlockMember("lightInfo", offsetof(FrameUniform, lightInfo), sizeof(glm::uvec4))};
}

auto shadowViewMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockMember("lightViewProjection", 0, sizeof(glm::mat4))};
}

auto materialRecordMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockArrayMember(
        "records",
        sizeof(MaterialRecord),
        {hostBlockMember("metallic", offsetof(MaterialRecord, metallic), sizeof(float)),
         hostBlockMember("roughness", offsetof(MaterialRecord, roughness), sizeof(float)),
         hostBlockMember("isLit", offsetof(MaterialRecord, isLit), sizeof(std::uint32_t)),
         hostBlockMember("renderFace", offsetof(MaterialRecord, renderFace), sizeof(std::uint32_t)),
         hostBlockMember("alphaCutoff", offsetof(MaterialRecord, alphaCutoff), sizeof(float)),
         hostBlockMember("hasBaseColorTexture", offsetof(MaterialRecord, hasBaseColorTexture), sizeof(std::uint32_t)),
         hostBlockMember("hasMetallicRoughnessTexture",
                         offsetof(MaterialRecord, hasMetallicRoughnessTexture),
                         sizeof(std::uint32_t)),
         hostBlockMember("hasEmissiveTexture", offsetof(MaterialRecord, hasEmissiveTexture), sizeof(std::uint32_t)),
         hostBlockMember("emissiveAndNormalScale", offsetof(MaterialRecord, emissiveAndNormalScale), sizeof(glm::vec4)),
         hostBlockMember("hasOcclusionTexture", offsetof(MaterialRecord, hasOcclusionTexture), sizeof(std::uint32_t)),
         hostBlockMember("occlusionStrength", offsetof(MaterialRecord, occlusionStrength), sizeof(float)),
         hostBlockMember("padding", offsetof(MaterialRecord, padding), sizeof(glm::vec2))})};
}

auto lightRecordMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockArrayMember(
        "records",
        sizeof(LightRecord),
        {hostBlockMember("positionRange", offsetof(LightRecord, positionRange), sizeof(glm::vec4)),
         hostBlockMember("directionOuterConeCos", offsetof(LightRecord, directionOuterConeCos), sizeof(glm::vec4)),
         hostBlockMember("colorIntensity", offsetof(LightRecord, colorIntensity), sizeof(glm::vec4)),
         hostBlockMember("innerConeCos", offsetof(LightRecord, innerConeCos), sizeof(float)),
         hostBlockMember("type", offsetof(LightRecord, type), sizeof(std::uint32_t)),
         hostBlockMember("shadowViewIndex", offsetof(LightRecord, shadowViewIndex), sizeof(std::uint32_t)),
         hostBlockMember("shadowViewCount", offsetof(LightRecord, shadowViewCount), sizeof(std::uint32_t))})};
}

auto shadowRecordMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockArrayMember(
        "records",
        sizeof(ShadowRecord),
        {hostBlockMember("viewProjection", offsetof(ShadowRecord, viewProjection), sizeof(glm::mat4)),
         hostBlockMember("atlasScaleBias", offsetof(ShadowRecord, atlasScaleBias), sizeof(glm::vec4)),
         hostBlockMember("shadowParameters", offsetof(ShadowRecord, shadowParameters), sizeof(glm::vec4))})};
}

auto storageRecordMembers(std::string_view name) -> std::optional<std::vector<ShaderBlockMember>>
{
    if (name == lightDataName)
    {
        return lightRecordMembers();
    }
    if (name == shadowDataName)
    {
        return shadowRecordMembers();
    }
    return std::nullopt;
}

auto hostBlockMembers(std::string_view name) -> std::optional<std::vector<ShaderBlockMember>>
{
    if (name == frameUniformName)
    {
        return frameUniformMembers();
    }
    if (name == shadowViewName)
    {
        return shadowViewMembers();
    }
    if (name == materialDataName)
    {
        return materialRecordMembers();
    }
    return storageRecordMembers(name);
}

auto drawConstantMembers() -> std::vector<ShaderBlockMember>
{
    return {hostBlockMember("modelMatrix", offsetof(DrawConstants, modelMatrix), sizeof(glm::mat4)),
            hostBlockMember("normalColumns", offsetof(DrawConstants, normalColumns), sizeof(glm::mat3x4)),
            hostBlockMember("tint", offsetof(DrawConstants, tint), sizeof(glm::vec4))};
}

auto shaderStageFlag(ShaderStage stage) -> vk::ShaderStageFlags;

auto validateBuiltinShaderStageBlock(std::string_view descriptorName,
                                     const ShaderProgramDescriptor::StageLayout& stage,
                                     std::span<const ShaderBlockMember> hostMembers) -> Result<void>
{
    const auto complete = descriptorName != frameUniformName;
    if (auto valid = validateShaderBlockMembers(stage.members, hostMembers, descriptorName, complete); !valid)
    {
        return valid;
    }
    if (descriptorName == frameUniformName && stage.members.size() == hostMembers.size() &&
        stage.blockSize != sizeof(FrameUniform))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Built-in FrameData block size does not match FrameUniform")};
    }
    return {};
}

auto validateBuiltinShaderDescriptor(const ShaderProgramDescriptor& descriptor) -> Result<void>
{
    if (descriptor.names.size() != 1)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Built-in shader descriptor must have one name")};
    }
    const auto name = std::string_view {descriptor.names.front()};
    const auto hostMembers = hostBlockMembers(name);
    if (!hostMembers)
    {
        return {};
    }
    for (const auto& stage : descriptor.stageLayouts)
    {
        if (auto valid = validateBuiltinShaderStageBlock(name, stage, *hostMembers); !valid)
        {
            return valid;
        }
    }
    return {};
}

auto validateBuiltinShaderBlocks(const ShaderProgramLayout& program) -> Result<void>
{
    for (const auto& descriptor : program.descriptors)
    {
        if (auto valid = validateBuiltinShaderDescriptor(descriptor); !valid)
        {
            return valid;
        }
    }
    return {};
}

auto validateBuiltinPushConstant(const ShaderPushConstantBlock& block,
                                 const std::optional<ShaderPushConstantBlock>& commonBlock,
                                 std::span<const ShaderBlockMember> hostMembers) -> Result<void>
{
    if (block.offset != 0 || block.size != sizeof(DrawConstants) ||
        (commonBlock && (commonBlock->offset != block.offset || commonBlock->size != block.size ||
                         commonBlock->members != block.members)))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Built-in push constants do not match DrawConstants")};
    }
    return validateShaderBlockMembers(block.members, hostMembers, "DrawConstants", true);
}

auto collectBuiltinPushConstantStages(const ShaderProgramLayout& program,
                                      const std::vector<ShaderBlockMember>& hostMembers,
                                      std::optional<ShaderPushConstantBlock>& commonBlock,
                                      vk::ShaderStageFlags& stages) -> Result<void>
{
    if (program.pushConstantBlocks.size() > 1)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Built-in shader declares multiple push constant ranges")};
    }
    for (const auto& range : program.pushConstantBlocks)
    {
        if (auto valid = validateBuiltinPushConstant(range.block, commonBlock, hostMembers); !valid)
        {
            return valid;
        }
        commonBlock = range.block;
        stages |= std::accumulate(range.stages.begin(),
                                  range.stages.end(),
                                  vk::ShaderStageFlags {},
                                  [](auto flags, auto stage) {
                                      return flags | shaderStageFlag(stage);
                                  });
    }
    return {};
}

auto builtinPushConstantRange(std::span<const ShaderProgramLayout> programs) -> Result<vk::PushConstantRange>
{
    auto commonBlock = std::optional<ShaderPushConstantBlock> {};
    auto stages = vk::ShaderStageFlags {};
    const auto hostMembers = drawConstantMembers();
    for (const auto& program : programs)
    {
        if (auto valid = collectBuiltinPushConstantStages(program, hostMembers, commonBlock, stages); !valid)
        {
            return std::unexpected {valid.error()};
        }
    }
    if (!commonBlock || !stages)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Built-in shaders do not declare DrawConstants")};
    }
    return vk::PushConstantRange {.stageFlags = stages, .offset = commonBlock->offset, .size = commonBlock->size};
}

auto makeBufferWrite(vk::DescriptorSet set,
                     const vk::DescriptorSetLayoutBinding& binding,
                     const vk::DescriptorBufferInfo& info) -> vk::WriteDescriptorSet
{
    return {.dstSet = set,
            .dstBinding = binding.binding,
            .descriptorCount = binding.descriptorCount,
            .descriptorType = binding.descriptorType,
            .pBufferInfo = &info};
}

auto makeImageWrite(vk::DescriptorSet set,
                    const vk::DescriptorSetLayoutBinding& binding,
                    const vk::DescriptorImageInfo& info) -> vk::WriteDescriptorSet
{
    return {.dstSet = set,
            .dstBinding = binding.binding,
            .descriptorCount = binding.descriptorCount,
            .descriptorType = binding.descriptorType,
            .pImageInfo = &info};
}

auto shaderStageFlag(ShaderStage stage) -> vk::ShaderStageFlags
{
    switch (stage)
    {
    case ShaderStage::Vertex:
        return vk::ShaderStageFlagBits::eVertex;
    case ShaderStage::Fragment:
        return vk::ShaderStageFlagBits::eFragment;
    }
    return {};
}

auto reflectBuiltinShaderPrograms() -> Result<std::vector<ShaderProgramLayout>>
{
    auto layouts = std::vector<ShaderProgramLayout> {};
    layouts.reserve(builtinShaderPrograms.size());
    for (const auto& program : builtinShaderPrograms)
    {
        auto layout = reflectGraphicsProgram(program.vertex, program.fragment, program.name);
        if (!layout)
        {
            return std::unexpected {layout.error()};
        }
        layouts.push_back(std::move(*layout));
    }
    return layouts;
}

using ReflectedDescriptors = std::map<std::string, ReflectedDescriptorInterface, std::less<>>;
using BindingNames = std::map<std::pair<std::uint32_t, std::uint32_t>, std::string>;

auto sameBinding(const ReflectedDescriptorInterface& current, const ShaderProgramDescriptor& descriptor) -> bool
{
    return current.set == descriptor.set && current.binding == descriptor.binding &&
           current.count == descriptor.count && current.kind == descriptor.kind;
}

auto descriptorStages(const ShaderProgramDescriptor& descriptor) -> vk::ShaderStageFlags
{
    return std::accumulate(descriptor.stageLayouts.begin(),
                           descriptor.stageLayouts.end(),
                           vk::ShaderStageFlags {},
                           [](auto stages, const auto& declaration) {
                               return stages | shaderStageFlag(declaration.stage);
                           });
}

auto addDescriptor(ReflectedDescriptors& descriptors,
                   BindingNames& bindingNames,
                   const ShaderProgramDescriptor& descriptor) -> Result<void>
{
    if (descriptor.names.size() != 1)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Built-in shader descriptor must have one reflected name")};
    }
    const auto& name = descriptor.names.front();
    const auto [bindingName, newBinding] =
        bindingNames.try_emplace(std::pair {descriptor.set, descriptor.binding}, name);
    if (!newBinding && bindingName->second != name)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          std::format("Built-in shaders assign different names to set {}, "
                                                      "binding {}",
                                                      descriptor.set,
                                                      descriptor.binding))};
    }
    const auto [entry, inserted] = descriptors.try_emplace(name,
                                                           ReflectedDescriptorInterface {.set = descriptor.set,
                                                                                         .binding = descriptor.binding,
                                                                                         .count = descriptor.count,
                                                                                         .kind = descriptor.kind,
                                                                                         .stages = {}});
    if (!inserted && !sameBinding(entry->second, descriptor))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          std::format("Built-in shaders disagree about descriptor '{}'", name))};
    }
    entry->second.stages |= descriptorStages(descriptor);
    return {};
}

auto addProgramDescriptors(ReflectedDescriptors& descriptors,
                           BindingNames& bindingNames,
                           const ShaderProgramLayout& program) -> Result<void>
{
    for (const auto& descriptor : program.descriptors)
    {
        if (auto added = addDescriptor(descriptors, bindingNames, descriptor); !added)
        {
            return added;
        }
    }
    return {};
}

auto collectBuiltinDescriptorInterfaces(std::span<const ShaderProgramLayout> programs) -> Result<ReflectedDescriptors>
{
    auto descriptors = ReflectedDescriptors {};
    auto bindingNames = BindingNames {};
    for (const auto& program : programs)
    {
        if (auto added = addProgramDescriptors(descriptors, bindingNames, program); !added)
        {
            return std::unexpected {std::move(added.error())};
        }
    }
    return descriptors;
}

auto descriptorType(ShaderDescriptorKind kind, std::string_view name) -> std::optional<vk::DescriptorType>
{
    switch (kind)
    {
    case ShaderDescriptorKind::UniformBuffer:
        return name == shadowViewName ? vk::DescriptorType::eUniformBufferDynamic : vk::DescriptorType::eUniformBuffer;
    case ShaderDescriptorKind::StorageBuffer:
        return vk::DescriptorType::eStorageBuffer;
    case ShaderDescriptorKind::CombinedImageSampler:
        return vk::DescriptorType::eCombinedImageSampler;
    case ShaderDescriptorKind::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

auto reflectedShaderBinding(const std::map<std::string, ReflectedDescriptorInterface, std::less<>>& descriptors,
                            std::string_view name,
                            ShaderDescriptorKind expectedKind) -> Result<ReflectedDescriptorBinding>
{
    const auto descriptor = descriptors.find(name);
    if (descriptor == descriptors.end())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          std::format("Built-in shaders do not declare descriptor '{}'", name))};
    }
    if (descriptor->second.kind != expectedKind || descriptor->second.count != 1)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          std::format("Built-in shader descriptor '{}' has an invalid type", name))};
    }
    const auto type = descriptorType(descriptor->second.kind, name);
    if (!type || !descriptor->second.stages)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, std::format("Built-in shader descriptor '{}' is unsupported", name))};
    }
    return ReflectedDescriptorBinding {
        .set = descriptor->second.set,
        .layout = {.binding = descriptor->second.binding,
                   .descriptorType = *type,
                   .descriptorCount = descriptor->second.count,
                   .stageFlags = descriptor->second.stages}
    };
}

auto isKnownBuiltinDescriptor(std::string_view name) -> bool
{
    return std::ranges::find(frameDescriptorDefinitions, name, &ShaderDescriptorDefinition::name) !=
               frameDescriptorDefinitions.end() ||
           std::ranges::find(materialTextureSlots, name, &MaterialTextureSlot::shaderName) !=
               materialTextureSlots.end();
}

auto validateDescriptorSets(BuiltinShaderBindings& bindings) -> Result<void>
{
    bindings.frameSet = bindings.descriptors.at(std::string {frameUniformName}).set;
    bindings.textureSet = bindings.descriptors.at(std::string {materialTextureSlots.front().shaderName}).set;
    if (bindings.frameSet == bindings.textureSet ||
        std::ranges::any_of(frameDescriptorDefinitions,
                            [&](const auto& definition) {
                                return bindings.descriptors.at(std::string {definition.name}).set != bindings.frameSet;
                            }) ||
        std::ranges::any_of(materialTextureSlots, [&](const auto& slot) {
            return bindings.descriptors.at(std::string {slot.shaderName}).set != bindings.textureSet;
        }))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Built-in shader descriptors use inconsistent descriptor sets")};
    }
    return {};
}

auto populateBuiltinShaderBindings(const ReflectedDescriptors& descriptors, BuiltinShaderBindings& result)
    -> Result<void>
{
    for (const auto& definition : frameDescriptorDefinitions)
    {
        auto binding = reflectedShaderBinding(descriptors, definition.name, definition.kind);
        if (!binding)
        {
            return std::unexpected {binding.error()};
        }
        result.descriptors.emplace(std::string {definition.name}, *binding);
    }
    for (const auto& slot : materialTextureSlots)
    {
        auto binding = reflectedShaderBinding(descriptors, slot.shaderName, ShaderDescriptorKind::CombinedImageSampler);
        if (!binding)
        {
            return std::unexpected {binding.error()};
        }
        result.descriptors.emplace(std::string {slot.shaderName}, *binding);
    }
    return validateDescriptorSets(result);
}

auto makeBuiltinShaderBindings(const ReflectedDescriptors& descriptors, BuiltinShaderBindings& result) -> Result<void>
{
    for (const auto& [name, descriptor] : descriptors)
    {
        static_cast<void>(descriptor);
        if (!isKnownBuiltinDescriptor(name))
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, std::format("Unknown built-in shader descriptor '{}'", name))};
        }
    }
    if (auto populated = populateBuiltinShaderBindings(descriptors, result); !populated)
    {
        return populated;
    }
    result.shaderDescriptorSchema.reserve(descriptors.size());
    for (const auto& [name, descriptor] : descriptors)
    {
        result.shaderDescriptorSchema.push_back({.name = name,
                                                 .set = descriptor.set,
                                                 .binding = descriptor.binding,
                                                 .count = descriptor.count,
                                                 .kind = descriptor.kind,
                                                 .arrayDimensions = {},
                                                 .members = {}});
    }
    return {};
}

auto validateBuiltinShaderPrograms(std::span<const ShaderProgramLayout> programs) -> Result<void>
{
    for (const auto& program : programs)
    {
        if (auto valid = validateBuiltinShaderBlocks(program); !valid)
        {
            return valid;
        }
    }
    return {};
}

auto buildBuiltinShaderBindings(std::span<const ShaderProgramLayout> programs, BuiltinShaderBindings& result)
    -> Result<void>
{
    auto pushConstantRange = builtinPushConstantRange(programs);
    if (!pushConstantRange)
    {
        return std::unexpected {pushConstantRange.error()};
    }
    auto descriptors = collectBuiltinDescriptorInterfaces(programs);
    if (!descriptors)
    {
        return std::unexpected {descriptors.error()};
    }
    if (auto populated = makeBuiltinShaderBindings(*descriptors, result); !populated)
    {
        return populated;
    }
    result.pushConstantRange = *pushConstantRange;
    return {};
}

auto builtinShaderBindings(BuiltinShaderBindings& result) -> Result<void>
{
    return reflectBuiltinShaderPrograms().and_then([&](auto programs) {
        return validateBuiltinShaderPrograms(programs).and_then([&] {
            return buildBuiltinShaderBindings(programs, result);
        });
    });
}

auto descriptorBindingsForSet(const std::map<std::string, ReflectedDescriptorBinding, std::less<>>& descriptors,
                              std::uint32_t set) -> std::vector<vk::DescriptorSetLayoutBinding>
{
    auto bindings = std::vector<vk::DescriptorSetLayoutBinding> {};
    for (const auto& [name, descriptor] : descriptors)
    {
        std::ignore = name;
        if (descriptor.set == set)
        {
            bindings.push_back(descriptor.layout);
        }
    }
    std::ranges::sort(bindings, {}, &vk::DescriptorSetLayoutBinding::binding);
    return bindings;
}

auto createDescriptorSetLayout(const vk::raii::Device& device, std::span<const vk::DescriptorSetLayoutBinding> bindings)
    -> Result<vk::DescriptorSetLayout>
{
    const auto info = vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                                         .pBindings = bindings.data()};
    const auto created = (*device).createDescriptorSetLayout(info, nullptr, *device.getDispatcher());
    if (created.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created.result)};
    }
    return created.value;
}

}

auto FrameDescriptors::initialize(const vk::PhysicalDeviceLimits& limits, std::size_t frames) -> Result<void>
{
    return reflectShaderBindings(limits.maxBoundDescriptorSets)
        .and_then([this] {
            return createLayout(_frameDescriptorSetIndex).transform([this](auto layout) {
                _descriptorSetLayout = vk::raii::DescriptorSetLayout {_device, layout};
            });
        })
        .and_then([this] {
            return createLayout(_textureDescriptorSetIndex).transform([this](auto layout) {
                _textureDescriptorSetLayout = vk::raii::DescriptorSetLayout {_device, layout};
            });
        })
        .and_then([this] {
            return initializePipelineLayout();
        })
        .and_then([&] {
            const auto bindings = descriptorBindingsForSet(_shaderBindings, _frameDescriptorSetIndex);
            return createDescriptorSets(_device, *_descriptorSetLayout, bindings, frames).transform([this](auto sets) {
                _descriptors = std::move(sets);
            });
        });
}

auto FrameDescriptors::createLayout(std::uint32_t set) const -> Result<vk::DescriptorSetLayout>
{
    return createDescriptorSetLayout(_device, descriptorBindingsForSet(_shaderBindings, set));
}

auto FrameDescriptors::materialTextureBindings() const -> std::vector<vk::DescriptorSetLayoutBinding>
{
    auto result = std::vector<vk::DescriptorSetLayoutBinding> {};
    result.reserve(materialTextureSlots.size());
    std::ranges::transform(materialTextureSlots, std::back_inserter(result), [this](const auto& slot) {
        return shaderBinding(slot.shaderName).layout;
    });
    return result;
}

auto FrameDescriptors::textureLayout() const noexcept -> vk::DescriptorSetLayout
{
    return *_textureDescriptorSetLayout;
}

auto FrameDescriptors::reflectShaderBindings(std::uint32_t maxSets) -> std::expected<void, Error>
{
    auto bindings = BuiltinShaderBindings {};
    if (auto reflected = builtinShaderBindings(bindings); !reflected)
    {
        return reflected;
    }
    if (bindings.frameSet >= maxSets || bindings.textureSet >= maxSets)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Built-in shader descriptor sets exceed device limits")};
    }
    _shaderBindings = std::move(bindings.descriptors);
    _shaderDescriptorSchema = std::move(bindings.shaderDescriptorSchema);
    _pushConstantRange = bindings.pushConstantRange;
    _frameDescriptorSetIndex = bindings.frameSet;
    _textureDescriptorSetIndex = bindings.textureSet;
    return {};
}

auto FrameDescriptors::shaderBinding(std::string_view name) const -> const ReflectedDescriptorBinding&
{
    const auto binding = _shaderBindings.find(name);
    ::panda::expect(binding != _shaderBindings.end(), "Built-in shader descriptor binding was not reflected");
    return binding->second;
}

auto FrameDescriptors::descriptorSetLayouts() -> Result<std::vector<vk::DescriptorSetLayout>>
{
    const auto setCount = std::max(_frameDescriptorSetIndex, _textureDescriptorSetIndex) + 1U;
    auto descriptorSetLayouts = std::vector<vk::DescriptorSetLayout>(setCount);
    _emptyDescriptorSetLayouts.reserve(setCount - 2U);
    descriptorSetLayouts.at(_frameDescriptorSetIndex) = *_descriptorSetLayout;
    descriptorSetLayouts.at(_textureDescriptorSetIndex) = *_textureDescriptorSetLayout;
    for (auto& layout : descriptorSetLayouts)
    {
        if (layout)
        {
            continue;
        }
        auto emptyLayout = createDescriptorSetLayout(_device, {});
        if (!emptyLayout)
        {
            return std::unexpected {std::move(emptyLayout.error())};
        }
        _emptyDescriptorSetLayouts.emplace_back(_device, *emptyLayout);
        layout = *_emptyDescriptorSetLayouts.back();
    }
    return descriptorSetLayouts;
}

auto FrameDescriptors::initializePipelineLayout() -> Result<void>
{
    return descriptorSetLayouts().and_then([this](const auto& layouts) -> Result<void> {
        const auto layoutInfo =
            vk::PipelineLayoutCreateInfo {.setLayoutCount = static_cast<std::uint32_t>(layouts.size()),
                                          .pSetLayouts = layouts.data(),
                                          .pushConstantRangeCount = 1,
                                          .pPushConstantRanges = &_pushConstantRange};
        return checkedCreation(_device.createPipelineLayout(layoutInfo)).transform([this](auto layout) {
            _pipelineLayout = std::move(layout);
        });
    });
}

auto FrameDescriptors::initializeShadowSampler() -> std::expected<void, Error>
{
    return createVulkanSampler(_device,
                               vk::SamplerCreateInfo {.magFilter = vk::Filter::eNearest,
                                                      .minFilter = vk::Filter::eNearest,
                                                      .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                                      .maxAnisotropy = 1.0F,
                                                      .minLod = 0.0F,
                                                      .maxLod = 0.0F,
                                                      .borderColor = vk::BorderColor::eFloatOpaqueWhite})
        .transform([this](auto sampler) {
            _shadowSampler = std::move(sampler);
        });
}

auto FrameDescriptors::initializeEnvironmentSampler() -> std::expected<void, Error>
{
    return createVulkanSampler(_device,
                               vk::SamplerCreateInfo {.magFilter = vk::Filter::eLinear,
                                                      .minFilter = vk::Filter::eLinear,
                                                      .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                                      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                                      .maxAnisotropy = 1.0F,
                                                      .minLod = 0.0F,
                                                      .maxLod = vk::LodClampNone})
        .transform([this](auto sampler) {
            _environmentSampler = std::move(sampler);
        });
}

void FrameDescriptors::writeEnvironment(vk::DescriptorSet descriptorSet, const RenderView& view)
{
    if (view.environment)
    {
        const auto diffuseInfo = vk::DescriptorImageInfo {
            .sampler = *_environmentSampler,
            .imageView =
                ::panda::detail::TextureResource::getResource(view.environment->diffuseIrradiance)->getNativeView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        const auto specularInfo = vk::DescriptorImageInfo {
            .sampler = *_environmentSampler,
            .imageView =
                ::panda::detail::TextureResource::getResource(view.environment->specularRadiance)->getNativeView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        const auto writes =
            std::array {makeImageWrite(descriptorSet, shaderBinding(diffuseEnvironmentName).layout, diffuseInfo),
                        makeImageWrite(descriptorSet, shaderBinding(specularEnvironmentName).layout, specularInfo)};
        _device.updateDescriptorSets(writes, {});
    }
    if (view.skybox)
    {
        const auto skyboxInfo = vk::DescriptorImageInfo {
            .sampler = *_environmentSampler,
            .imageView = ::panda::detail::TextureResource::getResource(*view.skybox)->getNativeView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        const auto skyboxWrite = makeImageWrite(descriptorSet, shaderBinding(skyboxMapName).layout, skyboxInfo);
        _device.updateDescriptorSets(skyboxWrite, {});
    }
}

void FrameDescriptors::write(std::size_t index,
                             const FrameBufferDescriptors& buffers,
                             vk::ImageView shadowView,
                             const RenderView& view)
{
    const auto descriptorSet = getFrameDescriptorSet(index);
    const auto imageInfo = vk::DescriptorImageInfo {.sampler = *_shadowSampler,
                                                    .imageView = shadowView,
                                                    .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    const auto writes =
        std::array {makeBufferWrite(descriptorSet, shaderBinding(frameUniformName).layout, buffers.uniform),
                    makeImageWrite(descriptorSet, shaderBinding(shadowMapName).layout, imageInfo),
                    makeBufferWrite(descriptorSet, shaderBinding(materialDataName).layout, buffers.material),
                    makeBufferWrite(descriptorSet, shaderBinding(lightDataName).layout, buffers.lights),
                    makeBufferWrite(descriptorSet, shaderBinding(shadowDataName).layout, buffers.shadows),
                    makeBufferWrite(descriptorSet, shaderBinding(shadowViewName).layout, buffers.shadowView)};
    _device.updateDescriptorSets(writes, {});
    writeEnvironment(descriptorSet, view);
}

auto FrameDescriptors::getPipelineLayout() const noexcept -> vk::PipelineLayout
{
    return *_pipelineLayout;
}

auto FrameDescriptors::getFrameDescriptorSetIndex() const noexcept -> std::uint32_t
{
    return _frameDescriptorSetIndex;
}

auto FrameDescriptors::getTextureDescriptorSetIndex() const noexcept -> std::uint32_t
{
    return _textureDescriptorSetIndex;
}

auto FrameDescriptors::getFrameDescriptorSet(std::size_t index) const -> vk::DescriptorSet
{
    return *_descriptors.sets.at(index);
}

auto FrameDescriptors::getShaderDescriptorSchema() const noexcept -> const std::vector<ShaderDescriptor>&
{
    return _shaderDescriptorSchema;
}
}

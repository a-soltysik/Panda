// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/ApplicationShaderBindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <panda/rendering/ShaderResources.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/Texture.hpp>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "VulkanError.hpp"
#include "renderer/DescriptorSets.hpp"
#include "renderer/FrameBindings.hpp"
#include "renderer/FrameDescriptors.hpp"
#include "renderer/ShaderProgramState.hpp"
#include "renderer/ShaderReflection.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
using ::panda::detail::BufferResource;

auto hasName(const ShaderProgramDescriptor& descriptor, std::string_view name) -> bool
{
    return std::ranges::find(descriptor.names, name) != descriptor.names.end();
}

auto getResources(const MeshDraw& draw) -> std::span<const ShaderResourceBinding>
{
    return draw.shaderResources ? draw.shaderResources->getBindings() : draw.material.shaderResources.getBindings();
}

auto bindingError(std::string_view name, std::string_view reason) -> Error
{
    return makeError(ErrorCode::InvalidArgument, std::format("Shader resource '{}': {}", name, reason));
}

auto isApplicationDescriptor(const ShaderProgramDescriptor& descriptor) -> bool
{
    return descriptor.kind == ShaderDescriptorKind::UniformBuffer ||
           descriptor.kind == ShaderDescriptorKind::StorageBuffer ||
           descriptor.kind == ShaderDescriptorKind::CombinedImageSampler;
}

auto nativeBinding(const ShaderProgramDescriptor& descriptor) -> vk::DescriptorSetLayoutBinding
{
    const auto stages =
        std::accumulate(descriptor.stageLayouts.begin(),
                        descriptor.stageLayouts.end(),
                        vk::ShaderStageFlags {},
                        [](auto flags, const auto& stage) {
                            return flags | (stage.stage == ShaderStage::Vertex ? vk::ShaderStageFlagBits::eVertex
                                                                               : vk::ShaderStageFlagBits::eFragment);
                        });
    const auto type = [&] {
        switch (descriptor.kind)
        {
        case ShaderDescriptorKind::UniformBuffer:
            return vk::DescriptorType::eUniformBuffer;
        case ShaderDescriptorKind::StorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case ShaderDescriptorKind::CombinedImageSampler:
            return vk::DescriptorType::eCombinedImageSampler;
        case ShaderDescriptorKind::Other:
            std::unreachable();
        }
        std::unreachable();
    }();
    return {.binding = descriptor.binding, .descriptorType = type, .descriptorCount = 1, .stageFlags = stages};
}

auto blockSize(const ShaderProgramDescriptor& descriptor) -> vk::DeviceSize
{
    const auto stage =
        std::ranges::max_element(descriptor.stageLayouts, {}, &ShaderProgramDescriptor::StageLayout::blockSize);
    return stage->blockSize;
}

auto validateNames(const ShaderProgramLayout& reflection,
                   std::uint32_t firstSet,
                   std::span<const ShaderResourceBinding> resources) -> Result<void>
{
    for (const auto& resource : resources)
    {
        const auto matches = std::ranges::count_if(reflection.descriptors, [&](const auto& binding) {
            return binding.set >= firstSet && hasName(binding, resource.name);
        });
        if (matches != 1)
        {
            return std::unexpected {bindingError(resource.name, "name is absent or ambiguous in the program")};
        }
    }
    return {};
}

auto findResource(const ShaderProgramDescriptor& binding, std::span<const ShaderResourceBinding> resources)
    -> Result<const ShaderResourceBinding*>
{
    auto matches = resources | std::views::filter([&](const auto& resource) {
                       return hasName(binding, resource.name);
                   });
    if (std::ranges::distance(matches) != 1)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          std::format("Expected one shader resource '{}'", binding.names.front()))};
    }
    return &*matches.begin();
}

auto countBindings(std::span<const vk::DescriptorSetLayoutBinding> bindings,
                   vk::DescriptorType type,
                   vk::ShaderStageFlags stages) -> std::uint64_t
{
    return std::accumulate(bindings.begin(), bindings.end(), std::uint64_t {}, [&](auto count, const auto& binding) {
        return binding.descriptorType == type && (binding.stageFlags & stages) ? count + binding.descriptorCount
                                                                               : count;
    });
}

auto supportsStage(std::span<const vk::DescriptorSetLayoutBinding> bindings,
                   const vk::PhysicalDeviceLimits& limits,
                   vk::ShaderStageFlagBits stage) -> bool
{
    const auto uniforms = countBindings(bindings, vk::DescriptorType::eUniformBuffer, stage) +
                          countBindings(bindings, vk::DescriptorType::eUniformBufferDynamic, stage);
    const auto storage = countBindings(bindings, vk::DescriptorType::eStorageBuffer, stage);
    const auto images = countBindings(bindings, vk::DescriptorType::eCombinedImageSampler, stage);
    const auto samplers = countBindings(bindings, vk::DescriptorType::eSampler, stage) + images;
    return uniforms <= limits.maxPerStageDescriptorUniformBuffers &&
           storage <= limits.maxPerStageDescriptorStorageBuffers &&
           images <= limits.maxPerStageDescriptorSampledImages && samplers <= limits.maxPerStageDescriptorSamplers &&
           uniforms + storage + images <= limits.maxPerStageResources;
}

auto validateLimits(std::span<const vk::DescriptorSetLayoutBinding> bindings, const vk::PhysicalDeviceLimits& limits)
    -> Result<void>
{
    constexpr auto graphics = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    if (countBindings(bindings, vk::DescriptorType::eUniformBuffer, graphics) > limits.maxDescriptorSetUniformBuffers ||
        countBindings(bindings, vk::DescriptorType::eStorageBuffer, graphics) > limits.maxDescriptorSetStorageBuffers ||
        countBindings(bindings, vk::DescriptorType::eCombinedImageSampler, graphics) >
            limits.maxDescriptorSetSampledImages ||
        countBindings(bindings, vk::DescriptorType::eCombinedImageSampler, graphics) >
            limits.maxDescriptorSetSamplers ||
        !supportsStage(bindings, limits, vk::ShaderStageFlagBits::eVertex) ||
        !supportsStage(bindings, limits, vk::ShaderStageFlagBits::eFragment))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Application shader bindings exceed device descriptor limits")};
    }
    return {};
}

using DescriptorBindings = std::map<std::pair<std::uint32_t, std::uint32_t>, vk::DescriptorSetLayoutBinding>;

auto mergeDescriptor(DescriptorBindings& descriptors, const ShaderProgramDescriptor& descriptor) -> Result<void>
{
    if (!isApplicationDescriptor(descriptor) || descriptor.names.empty())
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Application descriptors need a supported named resource")};
    }
    const auto binding = nativeBinding(descriptor);
    const auto key = std::pair {descriptor.set, descriptor.binding};
    const auto [entry, inserted] = descriptors.try_emplace(key, binding);
    if (inserted)
    {
        return {};
    }
    if (entry->second.descriptorType != binding.descriptorType ||
        entry->second.descriptorCount != binding.descriptorCount)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Shader variants disagree on an application descriptor")};
    }
    entry->second.stageFlags |= binding.stageFlags;
    return {};
}

auto mergeDescriptors(DescriptorBindings& descriptors, const ShaderProgramLayout& reflection, std::uint32_t firstSet)
    -> Result<void>
{
    for (const auto& descriptor : reflection.descriptors)
    {
        if (descriptor.set < firstSet)
        {
            continue;
        }
        if (auto merged = mergeDescriptor(descriptors, descriptor); !merged)
        {
            return merged;
        }
    }
    return {};
}

template <typename Program>
auto mergeProgramDescriptors(DescriptorBindings& descriptors,
                             const Program& program,
                             std::uint32_t firstSet,
                             const FrameDescriptors& frame) -> Result<void>
{
    if (program.camera.layout.descriptors.back().set >= frame.getLimits().maxBoundDescriptorSets)
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "Application shader set exceeds device limit")};
    }
    if (auto merged = mergeDescriptors(descriptors, program.camera.layout, firstSet); !merged)
    {
        return merged;
    }
    if (const auto* shadow = std::get_if<ProgramVariant>(&program.shadow); shadow != nullptr)
    {
        return mergeDescriptors(descriptors, shadow->layout, firstSet);
    }
    return {};
}

}

ApplicationShaderBindings::ApplicationShaderBindings(const vk::raii::Device& device,
                                                     FrameBindings& frameBindings,
                                                     resources::MemoryPool& pool,
                                                     std::size_t frameCount)
    : _device {device},
      _frameBindings {frameBindings},
      _pool {pool},
      _frames(frameCount)
{
}

auto ApplicationShaderBindings::createLayout(const ShaderProgram::State& program) const -> Result<ProgramLayout>
{
    const auto& frame = _frameBindings.getFrameDescriptors();
    const auto firstSet = frame.getApplicationSetIndex();
    const auto& camera = program.camera.layout;
    const auto lastSet = camera.descriptors.back().set;
    auto descriptors = DescriptorBindings {};
    if (auto merged = mergeProgramDescriptors(descriptors, program, firstSet, frame); !merged)
    {
        return std::unexpected {merged.error()};
    }
    auto sets = createSetLayouts(firstSet, lastSet, descriptors);
    if (!sets)
    {
        return std::unexpected {sets.error()};
    }
    if (auto valid = validateSetLayouts(*sets); !valid)
    {
        return std::unexpected {std::move(valid.error())};
    }
    return ProgramLayout {.sets = std::move(*sets)};
}

auto ApplicationShaderBindings::validateSetLayouts(std::span<const SetLayout> sets) const -> Result<void>
{
    const auto& frame = _frameBindings.getFrameDescriptors();
    auto bindings = frame.getLayoutBindings();
    for (const auto& set : sets)
    {
        bindings.insert(bindings.end(), set.bindings.begin(), set.bindings.end());
    }
    return validateLimits(bindings, frame.getLimits());
}

auto ApplicationShaderBindings::createSetLayouts(std::uint32_t firstSet,
                                                 std::uint32_t lastSet,
                                                 const DescriptorBindings& descriptors)
    -> Result<std::vector<SetLayout>>
{
    auto sets = std::vector<SetLayout>(lastSet - firstSet + 1U);
    for (const auto& [key, binding] : descriptors)
    {
        if (key.first > lastSet)
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Shadow application descriptor has no camera set layout")};
        }
        sets.at(key.first - firstSet).bindings.push_back(binding);
    }
    return sets;
}

auto ApplicationShaderBindings::createNativeSetLayouts(ProgramLayout& layout) const
    -> Result<std::vector<vk::DescriptorSetLayout>>
{
    auto native = std::vector<vk::DescriptorSetLayout> {};
    native.reserve(layout.sets.size());
    for (auto& set : layout.sets)
    {
        const auto info =
            vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(set.bindings.size()),
                                               .pBindings = set.bindings.data()};
        auto created = detail::checkedCreation(_device.createDescriptorSetLayout(info));
        if (!created)
        {
            return std::unexpected {std::move(created.error())};
        }
        set.layout = std::move(*created);
        native.push_back(*set.layout);
    }
    return native;
}

auto ApplicationShaderBindings::getOrCreateLayout(const std::shared_ptr<const ShaderProgram::State>& program)
    -> Result<const ProgramLayout*>
{
    if (const auto found = _layouts.find(program->revision); found != _layouts.end())
    {
        return &found->second;
    }
    auto layout = createLayout(*program);
    if (!layout)
    {
        return std::unexpected {std::move(layout.error())};
    }
    auto native = createNativeSetLayouts(*layout);
    if (!native)
    {
        return std::unexpected {std::move(native.error())};
    }
    return _frameBindings.getFrameDescriptors().createApplicationPipelineLayout(*native).transform([&](auto pipeline) {
        layout->pipeline = std::move(pipeline);
        return &(_layouts.emplace(program->revision, std::move(*layout)).first->second);
    });
}

auto ApplicationShaderBindings::prepare(std::size_t frameIndex, const RenderView& view) -> Result<void>
{
    auto bindings = std::map<std::size_t, DrawBindings> {};
    if (auto prepared = prepareMeshDraws(frameIndex, view.meshes, bindings); !prepared)
    {
        return prepared;
    }
    if (auto prepared = prepareProceduralDraws(frameIndex, view.proceduralDraws, view.meshes.size(), bindings);
        !prepared)
    {
        return prepared;
    }
    _frames.at(frameIndex).swap(bindings);
    return {};
}

auto ApplicationShaderBindings::prepareMeshDraws(std::size_t frameIndex,
                                                 std::span<const MeshDraw> draws,
                                                 std::map<std::size_t, DrawBindings>& bindings) -> Result<void>
{
    for (const auto& [index, draw] : std::views::enumerate(draws))
    {
        auto prepared = prepareDraw(frameIndex, draw);
        if (!prepared)
        {
            return std::unexpected {std::move(prepared.error())};
        }
        if (prepared->pipeline)
        {
            bindings.emplace(static_cast<std::size_t>(index), std::move(*prepared));
        }
    }
    return {};
}

auto ApplicationShaderBindings::prepareProceduralDraws(std::size_t frameIndex,
                                                       std::span<const ProceduralDraw> draws,
                                                       std::size_t firstDrawIndex,
                                                       std::map<std::size_t, DrawBindings>& bindings) -> Result<void>
{
    for (const auto& [index, draw] : std::views::enumerate(draws))
    {
        if (draw.vertexCount == 0 || draw.instanceCount == 0)
        {
            continue;
        }
        auto prepared = prepareProceduralDraw(frameIndex, draw);
        if (!prepared)
        {
            return std::unexpected {std::move(prepared.error())};
        }
        if (prepared->pipeline)
        {
            bindings.emplace(firstDrawIndex + static_cast<std::size_t>(index), std::move(*prepared));
        }
    }
    return {};
}

auto ApplicationShaderBindings::prepareDraw(std::size_t frameIndex, const MeshDraw& draw) -> Result<DrawBindings>
{
    return prepareProgram(frameIndex, draw.material.shaderProgram.snapshot(), getResources(draw));
}

auto ApplicationShaderBindings::prepareProceduralDraw(std::size_t frameIndex, const ProceduralDraw& draw)
    -> Result<DrawBindings>
{
    return prepareProgram(frameIndex, draw.shaderProgram.snapshot(), draw.shaderResources.getBindings());
}

auto ApplicationShaderBindings::prepareProgram(std::size_t frameIndex,
                                               const std::shared_ptr<const ShaderProgram::State>& program,
                                               std::span<const ShaderResourceBinding> resources) -> Result<DrawBindings>
{
    const auto hasApplicationBindings =
        program && !program->camera.layout.descriptors.empty() &&
        program->camera.layout.descriptors.back().set >= _frameBindings.getFrameDescriptors().getApplicationSetIndex();
    if (!hasApplicationBindings)
    {
        if (!resources.empty())
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Draw supplies resources without application bindings")};
        }
        return DrawBindings {.pipeline = {}, .sets = {}};
    }
    if (program->device != &_device)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shader program belongs to another Engine")};
    }
    return getOrCreateLayout(program).and_then([&](const auto* layout) {
        return bindResources(frameIndex, *program, resources, *layout);
    });
}

auto ApplicationShaderBindings::bindResources(std::size_t frameIndex,
                                              const ShaderProgram::State& program,
                                              std::span<const ShaderResourceBinding> resources,
                                              const ProgramLayout& layout) const -> Result<DrawBindings>
{
    const auto& reflection = program.camera.layout;
    const auto firstSet = _frameBindings.getFrameDescriptors().getApplicationSetIndex();
    if (auto valid = validateNames(reflection, firstSet, resources); !valid)
    {
        return std::unexpected {std::move(valid.error())};
    }
    return bindSets(frameIndex, reflection, resources, layout).transform([&](auto sets) {
        return DrawBindings {.pipeline = *layout.pipeline, .sets = std::move(sets)};
    });
}

auto ApplicationShaderBindings::bindSets(std::size_t frameIndex,
                                         const ShaderProgramLayout& reflection,
                                         std::span<const ShaderResourceBinding> resources,
                                         const ProgramLayout& layout) const -> Result<std::vector<BoundSet>>
{
    const auto firstSet = _frameBindings.getFrameDescriptors().getApplicationSetIndex();
    auto result = std::vector<BoundSet> {};
    for (const auto& [offset, set] : std::views::enumerate(layout.sets))
    {
        if (set.bindings.empty())
        {
            continue;
        }
        const auto index = firstSet + static_cast<std::uint32_t>(offset);
        auto bound = resolveResources(frameIndex, reflection, index, resources).and_then([&](auto resolved) {
            return prepareSet(set, index, std::move(resolved));
        });
        if (!bound)
        {
            return std::unexpected {std::move(bound.error())};
        }
        result.push_back(std::move(*bound));
    }
    return result;
}

auto ApplicationShaderBindings::resolveResources(std::size_t frameIndex,
                                                 const ShaderProgramLayout& reflection,
                                                 std::uint32_t setIndex,
                                                 std::span<const ShaderResourceBinding> resources) const
    -> Result<std::vector<BoundResource>>
{
    auto result = std::vector<BoundResource> {};
    auto descriptions = reflection.descriptors | std::views::filter([&](const auto& descriptor) {
                            return descriptor.set == setIndex;
                        });
    for (const auto& descriptor : descriptions)
    {
        auto bound = findResource(descriptor, resources).and_then([&](const auto* resource) {
            return createResource(frameIndex, descriptor, *resource);
        });
        if (!bound)
        {
            return std::unexpected {std::move(bound.error())};
        }
        result.push_back(std::move(*bound));
    }
    return result;
}

auto ApplicationShaderBindings::prepareSet(const SetLayout& layout,
                                           std::uint32_t index,
                                           std::vector<BoundResource> resources) const -> Result<BoundSet>
{
    return createDescriptorSets(_device, *layout.layout, layout.bindings, 1).transform([&](auto descriptors) {
        const auto writes = makeDescriptorWrites(*descriptors.sets.front(), layout.bindings, resources);
        _device.updateDescriptorSets(writes, {});
        return BoundSet {.index = index, .resources = std::move(resources), .descriptors = std::move(descriptors)};
    });
}

auto ApplicationShaderBindings::makeDescriptorWrites(vk::DescriptorSet set,
                                                     std::span<const vk::DescriptorSetLayoutBinding> bindings,
                                                     std::span<const BoundResource> resources)
    -> std::vector<vk::WriteDescriptorSet>
{
    auto writes = std::vector<vk::WriteDescriptorSet> {};
    writes.reserve(resources.size());
    for (const auto& [binding, resource] : std::views::zip(bindings, resources))
    {
        auto write = vk::WriteDescriptorSet {.dstSet = set,
                                             .dstBinding = binding.binding,
                                             .descriptorCount = 1,
                                             .descriptorType = binding.descriptorType};
        if (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler)
        {
            write.pImageInfo = &resource.imageInfo;
        }
        else
        {
            write.pBufferInfo = &resource.bufferInfo;
        }
        writes.push_back(write);
    }
    return writes;
}

auto ApplicationShaderBindings::createResource(std::size_t frameIndex,
                                               const ShaderProgramDescriptor& binding,
                                               const ShaderResourceBinding& resource) const -> Result<BoundResource>
{
    if (const auto* uniform = std::get_if<ShaderUniformBlock>(&resource.resource);
        uniform != nullptr && binding.kind == ShaderDescriptorKind::UniformBuffer)
    {
        return createUniform(binding, *uniform);
    }
    if (const auto* storage = std::get_if<ShaderBufferRange>(&resource.resource);
        storage != nullptr && binding.kind == ShaderDescriptorKind::StorageBuffer)
    {
        return resolveStorage(binding, *storage);
    }
    if (const auto* texture = std::get_if<Texture>(&resource.resource);
        texture != nullptr && binding.kind == ShaderDescriptorKind::CombinedImageSampler)
    {
        return resolveTexture(frameIndex, binding, *texture);
    }
    return std::unexpected {bindingError(resource.name, "resource kind differs from its GLSL declaration")};
}

auto ApplicationShaderBindings::createUniform(const ShaderProgramDescriptor& binding,
                                              const ShaderUniformBlock& value) const -> Result<BoundResource>
{
    const auto bytes = value.bytes.size();
    if (bytes == 0 || bytes != blockSize(binding) ||
        bytes > _frameBindings.getFrameDescriptors().getLimits().maxUniformBufferRange)
    {
        return std::unexpected {bindingError(binding.names.front(),
                                             "uniform byte size differs from reflection or exceeds the device limit")};
    }
    return BufferResource::createNative(
               _device,
               _pool,
               {.size = bytes, .usage = vk::BufferUsageFlagBits::eUniformBuffer, .memory = BufferMemory::Upload})
        .and_then([&](auto buffer) {
            return buffer->writeMapped(0, value.bytes).transform([&] {
                const auto native = buffer->getNative();
                return BoundResource {
                    .owner = std::move(buffer),
                    .texture = {},
                    .bufferInfo = {.buffer = native, .range = bytes}
                };
            });
        });
}

auto ApplicationShaderBindings::resolveStorage(const ShaderProgramDescriptor& binding,
                                               const ShaderBufferRange& value) const -> Result<BoundResource>
{
    const auto& buffer = BufferResource::getResource(value.buffer);
    if (!buffer || !buffer->belongsTo(_pool) || !(buffer->getUsage() & vk::BufferUsageFlagBits::eStorageBuffer) ||
        buffer->isExternallyOwned())
    {
        return std::unexpected {bindingError(binding.names.front(), "requires available same-Engine storage buffer")};
    }
    const auto& limits = _frameBindings.getFrameDescriptors().getLimits();
    if (value.offset > buffer->getSize() || value.size == 0 || value.size > buffer->getSize() - value.offset ||
        value.size < blockSize(binding) || value.size > limits.maxStorageBufferRange ||
        value.offset % limits.minStorageBufferOffsetAlignment != 0)
    {
        return std::unexpected {bindingError(binding.names.front(), "storage buffer range or alignment is invalid")};
    }
    return BoundResource {
        .owner = buffer,
        .texture = {},
        .bufferInfo = {.buffer = buffer->getNative(), .offset = value.offset, .range = value.size}
    };
}

auto ApplicationShaderBindings::resolveTexture(std::size_t frameIndex,
                                               const ShaderProgramDescriptor& binding,
                                               const Texture& value) const -> Result<BoundResource>
{
    if (!value.isValid() || !binding.sampledImage)
    {
        return std::unexpected {bindingError(binding.names.front(), "requires a sampled texture")};
    }
    const auto& texture = *::panda::detail::TextureResource::getResource(value);
    const auto& image = *binding.sampledImage;
    const auto expectedDimension =
        value.getType() == TextureType::CubeMap ? ShaderImageDimension::Cube : ShaderImageDimension::TwoDimensional;
    if (!texture.belongsTo(_pool) || !(texture.getUsage() & vk::ImageUsageFlagBits::eSampled) || !texture.isUploaded())
    {
        return std::unexpected {
            bindingError(binding.names.front(), "requires an uploaded sampled texture from this Engine")};
    }
    if (image.dimension != expectedDimension || image.sampleType != ShaderInterfaceFormat::Float || image.depth == 1 ||
        image.arrayed || image.multisampled || binding.count != 1)
    {
        return std::unexpected {bindingError(binding.names.front(), "texture type differs from its GLSL declaration")};
    }
    return _frameBindings.getTextureSampler(frameIndex, value).transform([&](vk::Sampler sampler) {
        return BoundResource {
            .owner = {},
            .texture = value,
            .imageInfo = {.sampler = sampler,
                      .imageView = texture.getNativeView(),
                      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal}
        };
    });
}

auto ApplicationShaderBindings::getPipelineLayout(const std::shared_ptr<const ShaderProgram::State>& program) const
    -> vk::PipelineLayout
{
    const auto found = _layouts.find(program->revision);
    return found != _layouts.end() ? *found->second.pipeline : _frameBindings.getFrameDescriptors().getPipelineLayout();
}

void ApplicationShaderBindings::bind(vk::CommandBuffer command, DrawBindingIndex index) const
{
    const auto& frame = _frames.at(index.frameIndex);
    if (const auto found = frame.find(index.drawIndex); found != frame.end())
    {
        for (const auto& set : found->second.sets)
        {
            const auto native = *set.descriptors.sets.front();
            command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       found->second.pipeline,
                                       set.index,
                                       1,
                                       &native,
                                       0,
                                       nullptr);
        }
    }
}

void ApplicationShaderBindings::bindForShadow(vk::CommandBuffer command,
                                              DrawBindingIndex index,
                                              const ShaderProgram& program) const
{
    const auto state = program.snapshot();
    if (state && std::holds_alternative<ProgramVariant>(state->shadow))
    {
        bind(command, index);
    }
}
}

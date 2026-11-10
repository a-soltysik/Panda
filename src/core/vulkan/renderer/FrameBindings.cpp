// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/geometric.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <span>
#include <utility>
#include <vector>

#include "VulkanError.hpp"
#include "renderer/MaterialTextureSlots.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderInterface.inc"
#include "renderer/ShadowView.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;

namespace
{
constexpr auto textureDescriptorsPerDraw = static_cast<std::uint32_t>(materialTextureSlots.size());

constexpr auto makeTextureBinding(std::uint32_t binding) -> vk::DescriptorSetLayoutBinding
{
    return {.binding = binding,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment};
}

constexpr auto makeTextureBindings() -> std::array<vk::DescriptorSetLayoutBinding, materialTextureSlots.size()>
{
    auto bindings = std::array<vk::DescriptorSetLayoutBinding, materialTextureSlots.size()> {};
    for (auto index = std::size_t {}; index < materialTextureSlots.size(); ++index)
    {
        bindings.at(index) = makeTextureBinding(materialTextureSlots.at(index).binding);
    }
    return bindings;
}

struct TextureDescriptorResources
{
    vk::raii::DescriptorPool pool {nullptr};
    std::vector<vk::raii::DescriptorSet> sets;
};

auto makeFrameUniform(const RenderView& view) -> std::expected<FrameUniform, Error>
{
    const auto lightViewProjection = createDirectionalShadowView(view.camera, view.directionalLight);
    if (!lightViewProjection)
    {
        return std::unexpected {lightViewProjection.error()};
    }
    const auto direction = glm::normalize(view.directionalLight.direction);
    return FrameUniform {
        .viewProjection = view.camera.viewProjection,
        .lightViewProjection = *lightViewProjection,
        .cameraPosition = glm::vec4 {view.camera.position, 1.0F},
        .lightDirection = glm::vec4 {direction, 0.0F},
        .lightColorIntensity = glm::vec4 {view.directionalLight.color, view.directionalLight.intensity},
        .ambient = glm::vec4 {1.0F, 1.0F, 1.0F, 0.06F}
    };
}

auto makeMaterialRecord(const Material& material) -> MaterialRecord
{
    return MaterialRecord {
        .metallic = std::clamp(material.metallic, 0.0F, 1.0F),
        .roughness = std::clamp(material.roughness, 0.0F, 1.0F),
        .isLit = material.model == MaterialModel::Lit ? 1U : 0U,
        .renderFace = static_cast<std::uint32_t>(material.renderFace),
        .alphaCutoff = material.alphaCutoff,
        .hasBaseColorTexture = material.baseColorTexture.isValid() ? 1U : 0U,
        .hasMetallicRoughnessTexture = material.metallicRoughnessTexture.isValid() ? 1U : 0U,
        .hasEmissiveTexture = material.emissiveTexture.isValid() ? 1U : 0U,
        .emissiveAndNormalScale = glm::vec4 {material.emissive, material.normalScale}
    };
}

auto makeTextureDescriptorInfo(const Texture& texture, vk::Sampler sampler, vk::ImageView fallbackView)
    -> vk::DescriptorImageInfo
{
    if (!texture.isValid())
    {
        return {.sampler = sampler, .imageView = fallbackView, .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    }
    return {.sampler = sampler,
            .imageView = ::panda::detail::TextureResource::getResource(texture)->getNativeView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
}

auto createTextureDescriptorResources(const vk::raii::Device& device,
                                      vk::DescriptorSetLayout layout,
                                      std::size_t drawCount) -> std::expected<TextureDescriptorResources, Error>
{
    if (drawCount > std::numeric_limits<std::uint32_t>::max() / textureDescriptorsPerDraw)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Material texture descriptor count exceeds Vulkan limits")};
    }
    const auto setCount = static_cast<std::uint32_t>(drawCount);
    const auto poolSize = vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler,
                                                  .descriptorCount = setCount * textureDescriptorsPerDraw};
    auto pool = checkedCreation(device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = setCount,
                                      .poolSizeCount = 1,
                                      .pPoolSizes = &poolSize}));
    if (!pool)
    {
        return std::unexpected {pool.error()};
    }
    const auto layouts = std::vector(setCount, layout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = **pool,
                                                           .descriptorSetCount = setCount,
                                                           .pSetLayouts = layouts.data()};
    auto sets = device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    return TextureDescriptorResources {.pool = std::move(*pool), .sets = std::move(sets.value)};
}
}

FrameBindings::FrameBindings(vk::PhysicalDevice physicalDevice,
                             const vk::raii::Device& device,
                             resources::MemoryPool& pool,
                             std::size_t frameCount)
    : _device {device},
      _pool {pool},
      _maxStorageBufferRange {physicalDevice.getProperties().limits.maxStorageBufferRange},
      _frames(frameCount)
{
}

FrameBindings::~FrameBindings() = default;

auto FrameBindings::validateDeviceLimits(vk::PhysicalDevice physicalDevice) -> std::expected<void, Error>
{
    if (physicalDevice.getProperties().limits.maxStorageBufferRange < sizeof(MaterialRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device storage-buffer limit is below renderer needs")};
    }
    return {};
}

auto FrameBindings::create(vk::PhysicalDevice physicalDevice,
                           const vk::raii::Device& device,
                           resources::MemoryPool& pool,
                           std::size_t frameCount) -> std::expected<std::unique_ptr<FrameBindings>, Error>
{
    if (const auto supported = validateDeviceLimits(physicalDevice); !supported)
    {
        return std::unexpected {supported.error()};
    }
    if (frameCount == 0 || frameCount > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid frame binding count")};
    }
    auto bindings = std::unique_ptr<FrameBindings> {
        new FrameBindings {physicalDevice, device, pool, frameCount}
    };
    if (const auto initialized = bindings->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return bindings;
}

auto FrameBindings::initialize() -> std::expected<void, Error>
{
    return initializeDescriptorState()
        .and_then([this] {
            return initializeSamplers();
        })
        .and_then([this] {
            return initializeFrameBuffers();
        });
}

auto FrameBindings::initializeDescriptorState() -> std::expected<void, Error>
{
    if (const auto descriptorSetLayout = initializeDescriptorSetLayout(); !descriptorSetLayout)
    {
        return descriptorSetLayout;
    }
    if (const auto textureDescriptorSetLayout = initializeTextureDescriptorSetLayout(); !textureDescriptorSetLayout)
    {
        return textureDescriptorSetLayout;
    }
    if (const auto pipelineLayout = initializePipelineLayout(); !pipelineLayout)
    {
        return pipelineLayout;
    }
    return initializeDescriptorPool();
}

auto FrameBindings::initializeDescriptorSetLayout() -> std::expected<void, Error>
{
    static constexpr auto bindings = std::array {
        vk::DescriptorSetLayoutBinding {
                                        .binding = PANDA_FRAME_UNIFORM_BINDING,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding {.binding = PANDA_FRAME_SHADOW_MAP_BINDING,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   },
        vk::DescriptorSetLayoutBinding {.binding = PANDA_FRAME_MATERIAL_DATA_BINDING,
                                        .descriptorType = vk::DescriptorType::eStorageBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   }
    };
    const auto setLayout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                           .pBindings = bindings.data()},
        nullptr,
        *_device.getDispatcher());
    if (setLayout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(setLayout.result)};
    }
    _descriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(setLayout.value)};
    return {};
}

auto FrameBindings::initializeTextureDescriptorSetLayout() -> std::expected<void, Error>
{
    static constexpr auto bindings = makeTextureBindings();
    const auto setLayout = (*_device).createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo {.bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                           .pBindings = bindings.data()},
        nullptr,
        *_device.getDispatcher());
    if (setLayout.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(setLayout.result)};
    }
    _textureDescriptorSetLayout =
        vk::raii::DescriptorSetLayout {_device, static_cast<vk::DescriptorSetLayout::CType>(setLayout.value)};
    return {};
}

auto FrameBindings::initializePipelineLayout() -> std::expected<void, Error>
{
    static constexpr auto pushRange = vk::PushConstantRange {.stageFlags = vk::ShaderStageFlagBits::eVertex,
                                                             .offset = 0,
                                                             .size = sizeof(DrawConstants)};
    const auto descriptorSetLayouts = std::array {*_descriptorSetLayout, *_textureDescriptorSetLayout};
    const auto layoutInfo =
        vk::PipelineLayoutCreateInfo {.setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
                                      .pSetLayouts = descriptorSetLayouts.data(),
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pushRange};
    auto pipelineLayout = checkedCreation(_device.createPipelineLayout(layoutInfo));
    if (!pipelineLayout)
    {
        return std::unexpected {pipelineLayout.error()};
    }
    _pipelineLayout = std::move(*pipelineLayout);
    return {};
}

auto FrameBindings::initializeDescriptorPool() -> std::expected<void, Error>
{
    const auto setCount = static_cast<std::uint32_t>(_frames.size());
    const auto poolSizes = std::array {
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eUniformBuffer,        .descriptorCount = setCount},
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = setCount},
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eStorageBuffer,        .descriptorCount = setCount}
    };
    auto pool = checkedCreation(_device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = setCount,
                                      .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
                                      .pPoolSizes = poolSizes.data()}));
    if (!pool)
    {
        return std::unexpected {pool.error()};
    }
    _descriptorPool = std::move(*pool);
    return allocateFrameDescriptorSets();
}

auto FrameBindings::allocateFrameDescriptorSets() -> std::expected<void, Error>
{
    const auto setCount = static_cast<std::uint32_t>(_frames.size());
    const auto layouts = std::vector(setCount, *_descriptorSetLayout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = *_descriptorPool,
                                                           .descriptorSetCount = setCount,
                                                           .pSetLayouts = layouts.data()};
    auto sets = _device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    for (auto index = std::size_t {}; index < _frames.size(); ++index)
    {
        _frames[index].descriptorSet = std::move(sets.value.at(index));
    }
    return {};
}

auto FrameBindings::initializeFrameBuffers() -> std::expected<void, Error>
{
    for (auto& frame : _frames)
    {
        static constexpr auto uniformDescription = BufferDesc {.size = sizeof(FrameUniform),
                                                               .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                                                               .memory = BufferMemory::Upload};
        auto uniformBuffer = ::panda::detail::BufferResource::create(_device, _pool, uniformDescription);
        if (!uniformBuffer)
        {
            return std::unexpected {uniformBuffer.error()};
        }
        frame.uniformBuffer = std::move(*uniformBuffer);
        static constexpr auto materialDescription = BufferDesc {.size = sizeof(MaterialRecord),
                                                                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                                                .memory = BufferMemory::Upload};
        auto materialBuffer = ::panda::detail::BufferResource::create(_device, _pool, materialDescription);
        if (!materialBuffer)
        {
            return std::unexpected {materialBuffer.error()};
        }
        frame.materialBuffer = std::move(*materialBuffer);
        frame.materialBufferCapacity = sizeof(MaterialRecord);
    }
    return {};
}

auto FrameBindings::initializeSamplers() -> std::expected<void, Error>
{
    if (const auto sampler = initializeShadowSampler(); !sampler)
    {
        return sampler;
    }
    return initializeTextureSampler();
}

auto FrameBindings::initializeShadowSampler() -> std::expected<void, Error>
{
    auto sampler = static_cast<vk::Device>(_device).createSampler(
        vk::SamplerCreateInfo {.magFilter = vk::Filter::eNearest,
                               .minFilter = vk::Filter::eNearest,
                               .mipmapMode = vk::SamplerMipmapMode::eNearest,
                               .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                               .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                               .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                               .maxAnisotropy = 1.0F,
                               .minLod = 0.0F,
                               .maxLod = 0.0F,
                               .borderColor = vk::BorderColor::eFloatOpaqueWhite});
    if (sampler.result != vk::Result::eSuccess)
    {
        return std::unexpected {::panda::makeVulkanError(sampler.result)};
    }
    _shadowSampler = vk::raii::Sampler {_device, sampler.value};
    return {};
}

auto FrameBindings::initializeTextureSampler() -> std::expected<void, Error>
{
    auto sampler = static_cast<vk::Device>(_device).createSampler(
        vk::SamplerCreateInfo {.magFilter = vk::Filter::eLinear,
                               .minFilter = vk::Filter::eLinear,
                               .mipmapMode = vk::SamplerMipmapMode::eNearest,
                               .addressModeU = vk::SamplerAddressMode::eRepeat,
                               .addressModeV = vk::SamplerAddressMode::eRepeat,
                               .addressModeW = vk::SamplerAddressMode::eRepeat,
                               .maxAnisotropy = 1.0F,
                               .minLod = 0.0F,
                               .maxLod = 0.0F});
    if (sampler.result != vk::Result::eSuccess)
    {
        return std::unexpected {::panda::makeVulkanError(sampler.result)};
    }
    _textureSampler = vk::raii::Sampler {_device, sampler.value};
    return {};
}

auto FrameBindings::writeFrameUniform(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view);
    if (!uniform)
    {
        return std::unexpected {uniform.error()};
    }
    const auto bytes = std::as_bytes(std::span {&*uniform, std::size_t {1}});
    return frame.uniformBuffer->writeMapped(0, bytes);
}

auto FrameBindings::ensureMaterialBuffer(FrameResources& frame, std::size_t drawCount) -> std::expected<void, Error>
{
    if (drawCount > _maxStorageBufferRange / sizeof(MaterialRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Mesh draw count exceeds material storage buffer limits")};
    }
    const auto requiredCapacity = sizeof(MaterialRecord) * std::max<std::size_t>(drawCount, 1);
    if (frame.materialBufferCapacity < requiredCapacity)
    {
        const auto description = BufferDesc {.size = requiredCapacity,
                                             .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                             .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::create(_device, _pool, description);
        if (!buffer)
        {
            return std::unexpected {buffer.error()};
        }
        frame.materialBuffer = std::move(*buffer);
        frame.materialBufferCapacity = requiredCapacity;
    }
    return {};
}

auto FrameBindings::ensureTextureDescriptorSets(FrameResources& frame, std::size_t drawCount)
    -> std::expected<void, Error>
{
    if (drawCount <= frame.textureDescriptorSets.size())
    {
        return {};
    }
    auto resources = createTextureDescriptorResources(_device, *_textureDescriptorSetLayout, drawCount);
    if (!resources)
    {
        return std::unexpected {resources.error()};
    }
    frame.textureDescriptorSets.clear();
    frame.textureDescriptorPool = std::move(resources->pool);
    frame.textureDescriptorSets = std::move(resources->sets);
    return {};
}

auto FrameBindings::writeMaterialRecords(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    _materialRecords.clear();
    _materialRecords.reserve(view.meshes.size());
    std::ranges::transform(view.meshes, std::back_inserter(_materialRecords), [](const auto& draw) {
        return makeMaterialRecord(draw.material);
    });
    if (!_materialRecords.empty())
    {
        const auto materialBytes = std::as_bytes(std::span {_materialRecords});
        if (const auto written = frame.materialBuffer->writeMapped(0, materialBytes); !written)
        {
            return written;
        }
    }
    return {};
}

void FrameBindings::updateFrameDescriptorSet(const FrameResources& frame, vk::ImageView shadowView)
{
    const auto bufferInfo = vk::DescriptorBufferInfo {.buffer = frame.uniformBuffer->getNative(),
                                                      .offset = 0,
                                                      .range = sizeof(FrameUniform)};
    const auto imageInfo = vk::DescriptorImageInfo {.sampler = *_shadowSampler,
                                                    .imageView = shadowView,
                                                    .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    const auto materialInfo = vk::DescriptorBufferInfo {.buffer = frame.materialBuffer->getNative(),
                                                        .offset = 0,
                                                        .range = frame.materialBufferCapacity};
    const auto writes = std::array {
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = PANDA_FRAME_UNIFORM_BINDING,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eUniformBuffer,
                                .pBufferInfo = &bufferInfo  },
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = PANDA_FRAME_SHADOW_MAP_BINDING,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &imageInfo    },
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = PANDA_FRAME_MATERIAL_DATA_BINDING,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eStorageBuffer,
                                .pBufferInfo = &materialInfo}
    };
    _device.updateDescriptorSets(writes, {});
}

void FrameBindings::writeTextureDescriptors(const FrameResources& frame,
                                            const RenderView& view,
                                            vk::ImageView shadowView)
{
    auto imageInfos = std::vector<vk::DescriptorImageInfo>(view.meshes.size() * textureDescriptorsPerDraw);
    auto writes = std::vector<vk::WriteDescriptorSet> {};
    writes.reserve(view.meshes.size() * textureDescriptorsPerDraw);
    for (auto index = std::size_t {}; index < view.meshes.size(); ++index)
    {
        const auto& material = view.meshes.at(index).material;
        const auto descriptorSet = *frame.textureDescriptorSets.at(index);
        for (auto slotIndex = std::size_t {}; slotIndex < materialTextureSlots.size(); ++slotIndex)
        {
            const auto& slot = materialTextureSlots.at(slotIndex);
            const auto imageInfoIndex = (index * textureDescriptorsPerDraw) + slotIndex;
            imageInfos.at(imageInfoIndex) =
                makeTextureDescriptorInfo(material.*(slot.texture), *_textureSampler, shadowView);
            writes.push_back({.dstSet = descriptorSet,
                              .dstBinding = slot.binding,
                              .descriptorCount = 1,
                              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                              .pImageInfo = &imageInfos.at(imageInfoIndex)});
        }
    }
    _device.updateDescriptorSets(writes, {});
}

auto FrameBindings::getPipelineLayout() const noexcept -> vk::PipelineLayout
{
    return *_pipelineLayout;
}

auto FrameBindings::getFrameDescriptorSet(std::size_t frameIndex) const -> vk::DescriptorSet
{
    return *_frames.at(frameIndex).descriptorSet;
}

auto FrameBindings::getTextureDescriptorSet(std::size_t frameIndex, std::size_t drawIndex) const -> vk::DescriptorSet
{
    return *_frames.at(frameIndex).textureDescriptorSets.at(drawIndex);
}

auto FrameBindings::updateFrame(std::size_t frameIndex, const RenderView& view, vk::ImageView shadowView)
    -> std::expected<void, Error>
{
    auto& frame = _frames.at(frameIndex);
    if (const auto written = writeFrameUniform(frame, view); !written)
    {
        return written;
    }
    if (const auto prepared = ensureMaterialBuffer(frame, view.meshes.size()); !prepared)
    {
        return prepared;
    }
    if (const auto written = writeMaterialRecords(frame, view); !written)
    {
        return written;
    }
    if (const auto ensured = ensureTextureDescriptorSets(frame, view.meshes.size()); !ensured)
    {
        return ensured;
    }
    updateFrameDescriptorSet(frame, shadowView);
    writeTextureDescriptors(frame, view, shadowView);
    return {};
}
}

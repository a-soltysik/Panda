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

constexpr auto makeFrameBindings() -> std::array<vk::DescriptorSetLayoutBinding, 4>
{
    return {
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
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   },
        vk::DescriptorSetLayoutBinding {.binding = PANDA_FRAME_LIGHT_DATA_BINDING,
                                        .descriptorType = vk::DescriptorType::eStorageBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   }
    };
}

auto makeBufferWrite(vk::DescriptorSet set,
                     std::uint32_t binding,
                     vk::DescriptorType type,
                     const vk::DescriptorBufferInfo& info) -> vk::WriteDescriptorSet
{
    return {.dstSet = set, .dstBinding = binding, .descriptorCount = 1, .descriptorType = type, .pBufferInfo = &info};
}

auto makeBufferInfo(vk::Buffer buffer, vk::DeviceSize range) -> vk::DescriptorBufferInfo
{
    return {.buffer = buffer, .offset = 0, .range = range};
}

struct TextureDescriptorResources
{
    vk::raii::DescriptorPool pool {nullptr};
    std::vector<vk::raii::DescriptorSet> sets;
};

auto makeFrameUniform(const RenderView& view, std::uint32_t lightCount) -> std::expected<FrameUniform, Error>
{
    auto lightViewProjection = glm::mat4 {1.0F};
    auto lightDirection = glm::vec3 {};
    if (!view.directionalLights.empty())
    {
        const auto& shadowLight = view.directionalLights.front();
        const auto shadowView = createDirectionalShadowView(view.camera, shadowLight);
        if (!shadowView)
        {
            return std::unexpected {shadowView.error()};
        }
        lightViewProjection = *shadowView;
        lightDirection = glm::normalize(shadowLight.direction);
    }
    return FrameUniform {
        .viewProjection = view.camera.viewProjection,
        .lightViewProjection = lightViewProjection,
        .cameraPosition = glm::vec4 {view.camera.position, 1.0F},
        .lightDirection = glm::vec4 {lightDirection, 0.0F},
        .ambient = glm::vec4 {1.0F, 1.0F, 1.0F, 0.06F},
        .lightInfo = glm::uvec4 {lightCount, 0U, 0U, 0U}
    };
}

auto makeLightRecord(const DirectionalLight& light) -> LightRecord
{
    return {
        .direction = glm::vec4 {glm::normalize(light.direction), 0.0F           },
        .colorIntensity = glm::vec4 {light.color,                     light.intensity}
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
        .emissiveAndNormalScale = glm::vec4 {material.emissive, material.normalScale},
        .hasOcclusionTexture = material.occlusionTexture.isValid() ? 1U : 0U,
        .occlusionStrength = std::clamp(material.occlusionStrength, 0.0F, 1.0F)
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

auto FrameBindings::validateDeviceLimits(vk::PhysicalDevice physicalDevice, std::uint32_t maxLights)
    -> std::expected<void, Error>
{
    return validateDeviceLimits(physicalDevice.getProperties().limits, maxLights);
}

auto FrameBindings::validateDeviceLimits(const vk::PhysicalDeviceLimits& limits, std::uint32_t maxLights)
    -> std::expected<void, Error>
{
    if (limits.maxStorageBufferRange < sizeof(MaterialRecord) ||
        limits.maxStorageBufferRange / sizeof(LightRecord) < maxLights || limits.maxDescriptorSetStorageBuffers < 2 ||
        limits.maxPerStageDescriptorStorageBuffers < 2)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device storage-buffer limits are below the configured renderer needs")};
    }
    return {};
}

auto FrameBindings::create(const FrameBindingsCreateInfo& info) -> std::expected<std::unique_ptr<FrameBindings>, Error>
{
    if (const auto supported = validateDeviceLimits(info.physicalDevice, info.maxLights); !supported)
    {
        return std::unexpected {supported.error()};
    }
    if (info.frameCount == 0 || info.frameCount > std::numeric_limits<std::uint32_t>::max() / 2U)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid frame binding count")};
    }
    auto bindings = std::unique_ptr<FrameBindings> {
        new FrameBindings {info.physicalDevice, info.device, info.pool, info.frameCount}
    };
    if (const auto initialized = bindings->initialize(info.maxLights); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return bindings;
}

auto FrameBindings::initialize(std::uint32_t maxLights) -> std::expected<void, Error>
{
    _maxLights = maxLights;
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
    static constexpr auto bindings = makeFrameBindings();
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
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eUniformBuffer,        .descriptorCount = setCount     },
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = setCount     },
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eStorageBuffer,        .descriptorCount = setCount * 2U}
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
        if (const auto initialized = initializeFrameBuffersForSlot(frame); !initialized)
        {
            return initialized;
        }
    }
    return {};
}

auto FrameBindings::initializeFrameBuffersForSlot(FrameResources& frame) -> std::expected<void, Error>
{
    return createUploadBuffer(sizeof(FrameUniform), vk::BufferUsageFlagBits::eUniformBuffer)
        .transform([&](auto buffer) {
            frame.uniformBuffer = std::move(buffer);
        })
        .and_then([&] {
            return createUploadBuffer(sizeof(MaterialRecord), vk::BufferUsageFlagBits::eStorageBuffer)
                .transform([&](auto buffer) {
                    frame.materialBuffer = std::move(buffer);
                    frame.materialBufferCapacity = sizeof(MaterialRecord);
                });
        })
        .and_then([&] {
            return createUploadBuffer(sizeof(LightRecord), vk::BufferUsageFlagBits::eStorageBuffer)
                .transform([&](auto buffer) {
                    frame.lightBuffer = std::move(buffer);
                    frame.lightBufferCapacity = sizeof(LightRecord);
                });
        });
}

auto FrameBindings::createUploadBuffer(vk::DeviceSize size, vk::BufferUsageFlagBits usage)
    -> std::expected<std::shared_ptr<::panda::detail::BufferResource>, Error>
{
    return ::panda::detail::BufferResource::create(
        _device,
        _pool,
        BufferDesc {.size = size, .usage = usage, .memory = BufferMemory::Upload});
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

auto FrameBindings::writeFrameUniform(FrameResources& frame, const RenderView& view, std::uint32_t lightCount)
    -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view, lightCount);
    if (!uniform)
    {
        return std::unexpected {uniform.error()};
    }
    const auto bytes = std::as_bytes(std::span {&*uniform, std::size_t {1}});
    return frame.uniformBuffer->writeMapped(0, bytes);
}

auto FrameBindings::ensureLightBuffer(FrameResources& frame, std::size_t lightCount) -> std::expected<void, Error>
{
    if (lightCount > _maxStorageBufferRange / sizeof(LightRecord))
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds storage-buffer limits")};
    }
    const auto requiredCapacity = sizeof(LightRecord) * std::max<std::size_t>(lightCount, 1U);
    if (frame.lightBufferCapacity < requiredCapacity)
    {
        const auto description = BufferDesc {.size = requiredCapacity,
                                             .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                             .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::create(_device, _pool, description);
        if (!buffer)
        {
            return std::unexpected {buffer.error()};
        }
        frame.lightBuffer = std::move(*buffer);
        frame.lightBufferCapacity = requiredCapacity;
    }
    return {};
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

auto FrameBindings::writeLightRecords(FrameResources& frame, const RenderView& view) -> std::expected<void, Error>
{
    if (view.directionalLights.empty())
    {
        return {};
    }
    _lightRecords.clear();
    _lightRecords.reserve(view.directionalLights.size());
    std::ranges::transform(view.directionalLights, std::back_inserter(_lightRecords), makeLightRecord);
    const auto lightBytes = std::as_bytes(std::span {_lightRecords});
    return frame.lightBuffer->writeMapped(0, lightBytes);
}

void FrameBindings::updateFrameDescriptorSet(const FrameResources& frame, vk::ImageView shadowView)
{
    const auto bufferInfo = makeBufferInfo(frame.uniformBuffer->getNative(), sizeof(FrameUniform));
    const auto imageInfo = vk::DescriptorImageInfo {.sampler = *_shadowSampler,
                                                    .imageView = shadowView,
                                                    .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    const auto materialInfo = makeBufferInfo(frame.materialBuffer->getNative(), frame.materialBufferCapacity);
    const auto lightInfo = makeBufferInfo(frame.lightBuffer->getNative(), frame.lightBufferCapacity);
    const auto writes = std::array {
        makeBufferWrite(*frame.descriptorSet,
                        PANDA_FRAME_UNIFORM_BINDING,
                        vk::DescriptorType::eUniformBuffer,
                        bufferInfo),
        vk::WriteDescriptorSet {.dstSet = *frame.descriptorSet,
                                .dstBinding = PANDA_FRAME_SHADOW_MAP_BINDING,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &imageInfo},
        makeBufferWrite(*frame.descriptorSet,
                        PANDA_FRAME_MATERIAL_DATA_BINDING,
                        vk::DescriptorType::eStorageBuffer,
                        materialInfo),
        makeBufferWrite(*frame.descriptorSet,
                        PANDA_FRAME_LIGHT_DATA_BINDING,
                        vk::DescriptorType::eStorageBuffer,
                        lightInfo)
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
    const auto requestedLightCount = view.directionalLights.size();
    if (requestedLightCount > _maxLights)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds the Context maxLights budget")};
    }
    const auto lightCount = static_cast<std::uint32_t>(requestedLightCount);
    return ensureLightBuffer(frame, lightCount)
        .and_then([&] {
            return writeFrameUniform(frame, view, lightCount);
        })
        .and_then([&] {
            return ensureMaterialBuffer(frame, view.meshes.size());
        })
        .and_then([&] {
            return writeMaterialRecords(frame, view);
        })
        .and_then([&] {
            return writeLightRecords(frame, view);
        })
        .and_then([&] {
            return ensureTextureDescriptorSets(frame, view.meshes.size());
        })
        .transform([&] {
            updateFrameDescriptorSet(frame, shadowView);
            writeTextureDescriptors(frame, view, shadowView);
        });
}
}

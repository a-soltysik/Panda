// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/FrameBindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/geometric.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/vulkan/Error.hpp>
#include <source_location>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "Overloaded.hpp"
#include "VulkanError.hpp"
#include "renderer/MaterialTextureSlots.hpp"
#include "renderer/ShaderData.hpp"
#include "renderer/ShaderInterface.inc"
#include "renderer/ShadowAtlas.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
using detail::checkedCreation;

namespace
{
constexpr auto textureDescriptorsPerDraw = static_cast<std::uint32_t>(materialTextureSlots.size());
constexpr auto ambientIntensity = 0.06F;

auto createVulkanSampler(const vk::raii::Device& device,
                         const vk::SamplerCreateInfo& createInfo,
                         std::source_location source = std::source_location::current())
    -> std::expected<vk::raii::Sampler, Error>
{
    const auto created = static_cast<vk::Device>(device).createSampler(createInfo);
    if (created.result != vk::Result::eSuccess)
    {
        return std::unexpected {::panda::makeVulkanError(created.result, source)};
    }
    return std::expected<vk::raii::Sampler, Error> {std::in_place, device, created.value};
}

auto shadowViewStride(std::uint64_t alignment) -> std::uint64_t
{
    const auto matrixSize = sizeof(glm::mat4);
    const auto effectiveAlignment = std::max(alignment, std::uint64_t {1});
    return matrixSize + ((effectiveAlignment - (matrixSize % effectiveAlignment)) % effectiveAlignment);
}

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

constexpr auto makeFrameBindings()
{
    return std::array {
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
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   },
        vk::DescriptorSetLayoutBinding {.binding = PANDA_FRAME_SHADOW_DATA_BINDING,
                                        .descriptorType = vk::DescriptorType::eStorageBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment                                   },
        vk::DescriptorSetLayoutBinding {.binding = PANDA_FRAME_SHADOW_VIEW_BINDING,
                                        .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex                                     }
    };
}

auto makeBufferWrite(vk::DescriptorSet set,
                     std::uint32_t binding,
                     vk::DescriptorType type,
                     const vk::DescriptorBufferInfo& info) -> vk::WriteDescriptorSet
{
    return {.dstSet = set, .dstBinding = binding, .descriptorCount = 1, .descriptorType = type, .pBufferInfo = &info};
}

auto makeImageWrite(vk::DescriptorSet set, std::uint32_t binding, const vk::DescriptorImageInfo& info)
    -> vk::WriteDescriptorSet
{
    return {.dstSet = set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &info};
}

struct TextureFilterSettings
{
    vk::Filter texelFilter;
    vk::SamplerMipmapMode mipmapMode;
};

auto mapTextureFilter(TextureFilteringPreset preset) -> TextureFilterSettings
{
    switch (preset)
    {
    case TextureFilteringPreset::Nearest:
        return {.texelFilter = vk::Filter::eNearest, .mipmapMode = vk::SamplerMipmapMode::eNearest};
    case TextureFilteringPreset::Bilinear:
        return {.texelFilter = vk::Filter::eLinear, .mipmapMode = vk::SamplerMipmapMode::eNearest};
    case TextureFilteringPreset::Trilinear:
    case TextureFilteringPreset::Anisotropic2x:
    case TextureFilteringPreset::Anisotropic4x:
    case TextureFilteringPreset::Anisotropic8x:
    case TextureFilteringPreset::Anisotropic16x:
        return {.texelFilter = vk::Filter::eLinear, .mipmapMode = vk::SamplerMipmapMode::eLinear};
    }
    return {.texelFilter = vk::Filter::eLinear, .mipmapMode = vk::SamplerMipmapMode::eLinear};
}

auto mapAddressMode(TextureAddressMode mode) -> vk::SamplerAddressMode
{
    switch (mode)
    {
    case TextureAddressMode::Repeat:
        return vk::SamplerAddressMode::eRepeat;
    case TextureAddressMode::MirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case TextureAddressMode::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    }
    return vk::SamplerAddressMode::eRepeat;
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

auto makeFrameUniform(const RenderView& view, std::uint32_t lightCount) -> FrameUniform
{
    return FrameUniform {
        .viewProjection = view.camera.viewProjection,
        .cameraPosition = glm::vec4 {view.camera.position, 1.0F},
        .ambient = glm::vec4 {1.0F, 1.0F, 1.0F, ambientIntensity},
        .lightInfo = glm::uvec4 {lightCount, 0U, 0U, 0U}
    };
}

auto makeDirectionalLightRecord(const DirectionalLight& light,
                                std::uint32_t shadowViewIndex,
                                std::uint32_t shadowViewCount) -> LightRecord
{
    return LightRecord {
        .positionRange = {},
        .directionOuterConeCos = glm::vec4 {glm::normalize(light.direction), 0.0F},
        .colorIntensity = glm::vec4 {light.color, light.intensity},
        .innerConeCos = 0.0F,
        .type = static_cast<std::uint32_t>(ShaderLightType::Directional),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makePointLightRecord(const PointLight& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount)
    -> LightRecord
{
    return LightRecord {
        .positionRange = glm::vec4 {light.position, light.range},
        .directionOuterConeCos = {},
        .colorIntensity = glm::vec4 {light.color, light.intensity},
        .innerConeCos = 0.0F,
        .type = static_cast<std::uint32_t>(ShaderLightType::Point),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makeSpotLightRecord(const SpotLight& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount)
    -> LightRecord
{
    return LightRecord {
        .positionRange = glm::vec4 {light.position,                  light.range                          },
        .directionOuterConeCos = glm::vec4 {glm::normalize(light.direction), std::cos(light.outerConeAngleRadians)},
        .colorIntensity = glm::vec4 {light.color,                     light.intensity                      },
        .innerConeCos = std::cos(light.innerConeAngleRadians),
        .type = static_cast<std::uint32_t>(ShaderLightType::Spot),
        .shadowViewIndex = shadowViewIndex,
        .shadowViewCount = shadowViewCount
    };
}

auto makeLightRecord(const Light& light, std::uint32_t shadowViewIndex, std::uint32_t shadowViewCount) -> LightRecord
{
    return std::visit(
        ::panda::detail::Overloaded {[=](const DirectionalLight& value) {
                                         return makeDirectionalLightRecord(value, shadowViewIndex, shadowViewCount);
                                     },
                                     [=](const PointLight& value) {
                                         return makePointLightRecord(value, shadowViewIndex, shadowViewCount);
                                     },
                                     [=](const SpotLight& value) {
                                         return makeSpotLightRecord(value, shadowViewIndex, shadowViewCount);
                                     }},
        light);
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

auto makeShadowRecord(const ShadowAtlasView& view) -> ShadowRecord
{
    const auto resolution = static_cast<float>(view.tile.resolution);
    return {
        .viewProjection = view.viewProjection,
        .atlasScaleBias = glm::vec4 {resolution / static_cast<float>(shadowAtlasExtent),
                                     resolution / static_cast<float>(shadowAtlasExtent),
                                     static_cast<float>(view.tile.x) / static_cast<float>(shadowAtlasExtent),
                                     static_cast<float>(view.tile.y) / static_cast<float>(shadowAtlasExtent)                                         },
        .shadowParameters = glm::vec4 {view.depthBias,                                     view.normalOffset, static_cast<float>(view.tile.layer), 0.0F}
    };
}

auto makeShadowViewMatrices(const ShadowAtlasPlan& plan, std::uint64_t stride) -> std::vector<std::byte>
{
    auto matrices = std::vector<std::byte>(stride * plan.views.size());
    for (auto index = std::size_t {}; index < plan.views.size(); ++index)
    {
        const auto matrixBytes = std::as_bytes(std::span {&plan.views.at(index).viewProjection, std::size_t {1}});
        auto destination = std::span {matrices}.subspan(index * stride, matrixBytes.size());
        std::ranges::copy(matrixBytes, destination.begin());
    }
    return matrices;
}

auto allocateTextureDescriptorSets(const vk::raii::Device& device,
                                   vk::DescriptorPool pool,
                                   vk::DescriptorSetLayout layout,
                                   std::uint32_t setCount) -> std::expected<std::vector<vk::raii::DescriptorSet>, Error>
{
    const auto layouts = std::vector(setCount, layout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = pool,
                                                           .descriptorSetCount = setCount,
                                                           .pSetLayouts = layouts.data()};
    auto sets = device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    return std::move(sets.value);
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
    const auto poolInfo = vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                                        .maxSets = setCount,
                                                        .poolSizeCount = 1,
                                                        .pPoolSizes = &poolSize};
    auto nativePool = vk::DescriptorPool {};
    const auto created = (*device).createDescriptorPool(&poolInfo, nullptr, &nativePool, *device.getDispatcher());
    if (created != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created)};
    }
    auto pool = vk::raii::DescriptorPool {device, nativePool};
    auto sets = allocateTextureDescriptorSets(device, *pool, layout, setCount);
    if (!sets)
    {
        return std::unexpected {sets.error()};
    }
    return TextureDescriptorResources {.pool = std::move(pool), .sets = std::move(*sets)};
}
}

FrameBindings::FrameBindings(const FrameBindingsCreateInfo& info)
    : _device {info.device},
      _pool {info.pool},
      _maxStorageBufferRange {info.physicalDevice.getProperties().limits.maxStorageBufferRange},
      _shadowViewStride {shadowViewStride(info.physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment)},
      _textureFilteringPreset {info.textureFilteringPreset},
      _frames(info.frameCount)
{
    initializeSamplerCapabilities(info.physicalDevice);
}

void FrameBindings::initializeSamplerCapabilities(vk::PhysicalDevice physicalDevice)
{
    auto features = vk::PhysicalDeviceFeatures2 {};
    physicalDevice.getFeatures2(&features);
    _samplerAnisotropySupported = features.features.samplerAnisotropy == vk::True;
    _maxSamplerAnisotropy = std::max(1.0F, physicalDevice.getProperties().limits.maxSamplerAnisotropy);
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
        limits.maxStorageBufferRange / sizeof(LightRecord) < maxLights || limits.maxDescriptorSetStorageBuffers < 3 ||
        limits.maxPerStageDescriptorStorageBuffers < 3 ||
        limits.minUniformBufferOffsetAlignment > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "Device descriptor-buffer limits are below renderer needs")};
    }
    return {};
}

auto FrameBindings::create(const FrameBindingsCreateInfo& info) -> std::expected<std::unique_ptr<FrameBindings>, Error>
{
    if (const auto supported = validateDeviceLimits(info.physicalDevice, info.maxLights); !supported)
    {
        return std::unexpected {supported.error()};
    }
    if (info.frameCount == 0 || info.frameCount > std::numeric_limits<std::uint32_t>::max() / 3U)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Invalid frame binding count")};
    }
    auto bindings = std::unique_ptr<FrameBindings> {new FrameBindings {info}};
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
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eUniformBufferDynamic, .descriptorCount = setCount     },
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = setCount     },
        vk::DescriptorPoolSize {.type = vk::DescriptorType::eStorageBuffer,        .descriptorCount = setCount * 3U}
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
    auto frame = _frames.begin();
    auto set = sets.value.begin();
    while (frame != _frames.end())
    {
        frame->descriptorSet = std::move(*set);
        ++frame;
        ++set;
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
        })
        .and_then([&] {
            return initializeFrameShadowBuffersForSlot(frame);
        });
}

auto FrameBindings::initializeFrameShadowBuffersForSlot(FrameResources& frame) -> std::expected<void, Error>
{
    return createUploadBuffer(sizeof(ShadowRecord), vk::BufferUsageFlagBits::eStorageBuffer)
        .transform([&](auto buffer) {
            frame.shadowBuffer = std::move(buffer);
            frame.shadowBufferCapacity = sizeof(ShadowRecord);
        })
        .and_then([&] {
            return createUploadBuffer(_shadowViewStride, vk::BufferUsageFlagBits::eUniformBuffer)
                .transform([&](auto buffer) {
                    frame.shadowViewBuffer = std::move(buffer);
                    frame.shadowViewBufferCapacity = _shadowViewStride;
                });
        });
}

auto FrameBindings::createUploadBuffer(vk::DeviceSize size, vk::BufferUsageFlagBits usage)
    -> std::expected<std::shared_ptr<::panda::detail::BufferResource>, Error>
{
    return ::panda::detail::BufferResource::createNative(
        _device,
        _pool,
        ::panda::detail::BufferResourceCreateInfo {.size = size, .usage = usage, .memory = BufferMemory::Upload});
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
    auto sampler = createVulkanSampler(_device,
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
    if (!sampler)
    {
        return std::unexpected {sampler.error()};
    }
    _shadowSampler = std::move(*sampler);
    return {};
}

auto FrameBindings::initializeTextureSampler() -> std::expected<void, Error>
{
    auto sampler = createVulkanSampler(_device,
                                       vk::SamplerCreateInfo {.magFilter = vk::Filter::eLinear,
                                                              .minFilter = vk::Filter::eLinear,
                                                              .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                              .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                              .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                              .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                              .maxAnisotropy = 1.0F,
                                                              .minLod = 0.0F,
                                                              .maxLod = 0.0F});
    if (!sampler)
    {
        return std::unexpected {sampler.error()};
    }
    _textureSampler = std::move(*sampler);
    return {};
}

auto FrameBindings::createTextureSampler(TextureAddressMode addressMode,
                                         std::uint32_t mipLevels,
                                         TextureFilteringPreset preset,
                                         float anisotropy) const -> std::expected<vk::raii::Sampler, Error>
{
    const auto filtering = mapTextureFilter(preset);
    return createVulkanSampler(_device,
                               vk::SamplerCreateInfo {.magFilter = filtering.texelFilter,
                                                      .minFilter = filtering.texelFilter,
                                                      .mipmapMode = filtering.mipmapMode,
                                                      .addressModeU = mapAddressMode(addressMode),
                                                      .addressModeV = mapAddressMode(addressMode),
                                                      .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                      .anisotropyEnable = anisotropy > 1.0F ? vk::True : vk::False,
                                                      .maxAnisotropy = anisotropy,
                                                      .minLod = 0.0F,
                                                      .maxLod = static_cast<float>(mipLevels - 1U)});
}

auto FrameBindings::getTextureSampler(FrameResources& frame, const Texture& texture)
    -> std::expected<vk::Sampler, Error>
{
    if (!texture.isValid())
    {
        return *_textureSampler;
    }
    const auto& resource = *::panda::detail::TextureResource::getResource(texture);
    const auto preset = requestedTextureFilteringPreset();
    const auto mipLevels = resource.getMipLevels();
    const auto anisotropy = mipLevels > 1 ? getEffectiveAnisotropy(preset) : 1.0F;
    const auto key = TextureSamplerKey {.addressMode = resource.getAddressMode(),
                                        .preset = preset,
                                        .mipLevels = mipLevels,
                                        .anisotropy = anisotropy};
    return getOrCreateTextureSampler(frame, key);
}

auto FrameBindings::getOrCreateTextureSampler(FrameResources& frame, const TextureSamplerKey& key)
    -> std::expected<vk::Sampler, Error>
{
    auto cached = std::ranges::find_if(frame.textureSamplers, [&](const TextureSampler& sampler) {
        return sampler.key == key;
    });
    if (cached != frame.textureSamplers.end())
    {
        cached->used = true;
        return *cached->sampler;
    }
    auto created = createTextureSampler(key.addressMode, key.mipLevels, key.preset, key.anisotropy);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    frame.textureSamplers.push_back(TextureSampler {.key = key, .sampler = std::move(*created), .used = true});
    return *frame.textureSamplers.back().sampler;
}

auto FrameBindings::writeFrameUniform(FrameResources& frame, const RenderView& view, std::uint32_t lightCount)
    -> std::expected<void, Error>
{
    const auto uniform = makeFrameUniform(view, lightCount);
    const auto bytes = std::as_bytes(std::span {&uniform, std::size_t {1}});
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
        const auto description =
            ::panda::detail::BufferResourceCreateInfo {.size = requiredCapacity,
                                                       .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                                       .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::createNative(_device, _pool, description);
        if (!buffer)
        {
            return std::unexpected {buffer.error()};
        }
        frame.lightBuffer = std::move(*buffer);
        frame.lightBufferCapacity = requiredCapacity;
    }
    return {};
}

auto FrameBindings::ensureShadowBuffers(FrameResources& frame, std::size_t shadowViewCount)
    -> std::expected<void, Error>
{
    const auto bufferCount = std::max<std::size_t>(shadowViewCount, 1U);
    if (bufferCount > _maxStorageBufferRange / sizeof(ShadowRecord) ||
        (shadowViewCount > 0 && (shadowViewCount - 1U) > std::numeric_limits<std::uint32_t>::max() / _shadowViewStride))
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Shadow views exceed Vulkan buffer limits")};
    }
    if (auto result = ensureShadowRecordBuffer(frame, bufferCount); !result)
    {
        return result;
    }
    return ensureShadowViewBuffer(frame, bufferCount);
}

auto FrameBindings::ensureShadowRecordBuffer(FrameResources& frame, std::size_t bufferCount)
    -> std::expected<void, Error>
{
    const auto requiredCapacity = sizeof(ShadowRecord) * bufferCount;
    if (frame.shadowBufferCapacity >= requiredCapacity)
    {
        return {};
    }
    auto buffer = createUploadBuffer(requiredCapacity, vk::BufferUsageFlagBits::eStorageBuffer);
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    frame.shadowBuffer = std::move(*buffer);
    frame.shadowBufferCapacity = requiredCapacity;
    return {};
}

auto FrameBindings::ensureShadowViewBuffer(FrameResources& frame, std::size_t bufferCount) -> std::expected<void, Error>
{
    const auto requiredCapacity = _shadowViewStride * bufferCount;
    if (frame.shadowViewBufferCapacity >= requiredCapacity)
    {
        return {};
    }
    auto buffer = createUploadBuffer(requiredCapacity, vk::BufferUsageFlagBits::eUniformBuffer);
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    frame.shadowViewBuffer = std::move(*buffer);
    frame.shadowViewBufferCapacity = requiredCapacity;
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
        const auto description =
            ::panda::detail::BufferResourceCreateInfo {.size = requiredCapacity,
                                                       .usage = vk::BufferUsageFlagBits::eStorageBuffer,
                                                       .memory = BufferMemory::Upload};
        auto buffer = ::panda::detail::BufferResource::createNative(_device, _pool, description);
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

auto FrameBindings::writeLightRecords(FrameResources& frame, const RenderView& view, const ShadowAtlasPlan& plan)
    -> std::expected<void, Error>
{
    if (view.lights.empty())
    {
        return {};
    }
    _lightRecords.clear();
    _lightRecords.reserve(view.lights.size());
    for (auto index = std::size_t {}; index < view.lights.size(); ++index)
    {
        _lightRecords.push_back(
            makeLightRecord(view.lights.at(index), plan.firstViewByLight.at(index), plan.viewCountByLight.at(index)));
    }
    const auto lightBytes = std::as_bytes(std::span {_lightRecords});
    return frame.lightBuffer->writeMapped(0, lightBytes);
}

auto FrameBindings::writeShadowRecords(FrameResources& frame, const ShadowAtlasPlan& plan) const
    -> std::expected<void, Error>
{
    if (plan.views.empty())
    {
        return {};
    }
    auto records = std::vector<ShadowRecord> {};
    records.reserve(plan.views.size());
    std::ranges::transform(plan.views, std::back_inserter(records), makeShadowRecord);
    const auto matrices = makeShadowViewMatrices(plan, _shadowViewStride);
    const auto recordBytes = std::as_bytes(std::span {records});
    if (const auto written = frame.shadowBuffer->writeMapped(0, recordBytes); !written)
    {
        return written;
    }
    return frame.shadowViewBuffer->writeMapped(0, matrices);
}

void FrameBindings::updateFrameDescriptorSet(const FrameResources& frame, vk::ImageView shadowView)
{
    const auto descriptorSet = *frame.descriptorSet;
    const auto uniformInfo = makeBufferInfo(frame.uniformBuffer->getNative(), sizeof(FrameUniform));
    const auto imageInfo = vk::DescriptorImageInfo {.sampler = *_shadowSampler,
                                                    .imageView = shadowView,
                                                    .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};
    const auto materialInfo = makeBufferInfo(frame.materialBuffer->getNative(), frame.materialBufferCapacity);
    const auto lightInfo = makeBufferInfo(frame.lightBuffer->getNative(), frame.lightBufferCapacity);
    const auto shadowInfo = makeBufferInfo(frame.shadowBuffer->getNative(), frame.shadowBufferCapacity);
    const auto shadowViewInfo = makeBufferInfo(frame.shadowViewBuffer->getNative(), sizeof(glm::mat4));
    const auto writes = std::array {
        makeBufferWrite(descriptorSet, PANDA_FRAME_UNIFORM_BINDING, vk::DescriptorType::eUniformBuffer, uniformInfo),
        makeImageWrite(descriptorSet, PANDA_FRAME_SHADOW_MAP_BINDING, imageInfo),
        makeBufferWrite(descriptorSet,
                        PANDA_FRAME_MATERIAL_DATA_BINDING,
                        vk::DescriptorType::eStorageBuffer,
                        materialInfo),
        makeBufferWrite(descriptorSet, PANDA_FRAME_LIGHT_DATA_BINDING, vk::DescriptorType::eStorageBuffer, lightInfo),
        makeBufferWrite(descriptorSet, PANDA_FRAME_SHADOW_DATA_BINDING, vk::DescriptorType::eStorageBuffer, shadowInfo),
        makeBufferWrite(descriptorSet,
                        PANDA_FRAME_SHADOW_VIEW_BINDING,
                        vk::DescriptorType::eUniformBufferDynamic,
                        shadowViewInfo)};
    _device.updateDescriptorSets(writes, {});
}

auto FrameBindings::writeTextureDescriptors(FrameResources& frame,
                                            const RenderView& view,
                                            vk::ImageView textureFallbackView) -> std::expected<void, Error>
{
    auto imageInfos = std::vector<vk::DescriptorImageInfo>(view.meshes.size() * textureDescriptorsPerDraw);
    auto writes = std::vector<vk::WriteDescriptorSet> {};
    for (auto& sampler : frame.textureSamplers)
    {
        sampler.used = false;
    }
    writes.reserve(view.meshes.size() * textureDescriptorsPerDraw);
    auto batch = TextureDescriptorWriteBatch {.frame = frame,
                                              .fallbackView = textureFallbackView,
                                              .imageInfos = imageInfos,
                                              .writes = writes};
    for (auto index = std::size_t {}; index < view.meshes.size(); ++index)
    {
        const auto appended = appendTextureDescriptorWrites(batch, index, view.meshes.at(index));
        if (!appended)
        {
            return std::unexpected {appended.error()};
        }
    }
    _device.updateDescriptorSets(writes, {});
    std::erase_if(frame.textureSamplers, [](const TextureSampler& sampler) {
        return !sampler.used;
    });
    return {};
}

auto FrameBindings::appendTextureDescriptorWrites(TextureDescriptorWriteBatch& batch,
                                                  std::size_t drawIndex,
                                                  const MeshDraw& draw) -> std::expected<void, Error>
{
    const auto descriptorSet = *batch.frame.textureDescriptorSets.at(drawIndex);
    for (auto slotIndex = std::size_t {}; slotIndex < materialTextureSlots.size(); ++slotIndex)
    {
        const auto& slot = materialTextureSlots.at(slotIndex);
        const auto imageInfoIndex = (drawIndex * textureDescriptorsPerDraw) + slotIndex;
        const auto& texture = draw.material.*(slot.texture);
        const auto sampler = getTextureSampler(batch.frame, texture);
        if (!sampler)
        {
            return std::unexpected {sampler.error()};
        }
        batch.imageInfos[imageInfoIndex] = makeTextureDescriptorInfo(texture, *sampler, batch.fallbackView);
        batch.writes.push_back({.dstSet = descriptorSet,
                                .dstBinding = slot.binding,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &batch.imageInfos[imageInfoIndex]});
    }
    return {};
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

auto FrameBindings::getShadowAtlasPlan(std::size_t frameIndex) const noexcept -> const ShadowAtlasPlan&
{
    return _frames.at(frameIndex).shadowPlan;
}

auto FrameBindings::getShadowViewOffset(std::size_t frameIndex, std::uint32_t shadowViewIndex) const -> std::uint32_t
{
    const auto& shadowViews = _frames.at(frameIndex).shadowPlan.views;
    ::panda::expect(shadowViewIndex < shadowViews.size(), "Shadow view index is out of range");
    return static_cast<std::uint32_t>(shadowViewIndex * _shadowViewStride);
}

auto FrameBindings::getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float
{
    auto requested = 1.0F;
    switch (preset)
    {
    case TextureFilteringPreset::Anisotropic2x:
        requested = 2.0F;
        break;
    case TextureFilteringPreset::Anisotropic4x:
        requested = 4.0F;
        break;
    case TextureFilteringPreset::Anisotropic8x:
        requested = 8.0F;
        break;
    case TextureFilteringPreset::Anisotropic16x:
        requested = 16.0F;
        break;
    case TextureFilteringPreset::Nearest:
    case TextureFilteringPreset::Bilinear:
    case TextureFilteringPreset::Trilinear:
        return 1.0F;
    }
    if (!_samplerAnisotropySupported)
    {
        return 1.0F;
    }
    return std::min(requested, _maxSamplerAnisotropy);
}

auto FrameBindings::requestedTextureFilteringPreset() const noexcept -> TextureFilteringPreset
{
    return _textureFilteringPreset ? *_textureFilteringPreset : TextureFilteringPreset::Anisotropic8x;
}

auto FrameBindings::updateFrameBuffers(FrameResources& frame, const RenderView& view, const ShadowAtlasPlan& plan)
    -> std::expected<void, Error>
{
    const auto lightCount = static_cast<std::uint32_t>(view.lights.size());
    return ensureLightBuffer(frame, lightCount)
        .and_then([&] {
            return ensureShadowBuffers(frame, plan.views.size());
        })
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
            return writeLightRecords(frame, view, plan);
        })
        .and_then([&] {
            return writeShadowRecords(frame, plan);
        });
}

auto FrameBindings::updateFrame(std::size_t frameIndex, const RenderView& view, vk::ImageView shadowView)
    -> std::expected<void, Error>
{
    return updateFrame(frameIndex, view, shadowView, shadowView);
}

auto FrameBindings::updateFrame(std::size_t frameIndex,
                                const RenderView& view,
                                vk::ImageView shadowArrayView,
                                vk::ImageView textureFallbackView) -> std::expected<void, Error>
{
    auto& frame = _frames.at(frameIndex);
    const auto requestedLightCount = view.lights.size();
    if (requestedLightCount > _maxLights)
    {
        return std::unexpected {
            makeError(ErrorCode::ResourceExhausted, "Render view light count exceeds the Engine maxLights budget")};
    }
    auto plan = buildShadowAtlasPlan(view);
    if (!plan)
    {
        return std::unexpected {plan.error()};
    }
    if (const auto updated = updateFrameBuffers(frame, view, *plan); !updated)
    {
        return updated;
    }
    if (const auto updated = updateFrameDescriptors(frame, view, shadowArrayView, textureFallbackView); !updated)
    {
        return updated;
    }
    frame.shadowPlan = std::move(*plan);
    return {};
}

auto FrameBindings::updateFrameDescriptors(FrameResources& frame,
                                           const RenderView& view,
                                           vk::ImageView shadowArrayView,
                                           vk::ImageView textureFallbackView) -> std::expected<void, Error>
{
    if (const auto updated = ensureTextureDescriptorSets(frame, view.meshes.size()); !updated)
    {
        return updated;
    }
    updateFrameDescriptorSet(frame, shadowArrayView);
    return writeTextureDescriptors(frame, view, textureFallbackView);
}
}

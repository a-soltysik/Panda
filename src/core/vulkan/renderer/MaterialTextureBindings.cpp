// clang-format off
#include <panda/base/Error.hpp>
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/MaterialTextureBindings.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Texture.hpp>
#include <utility>
#include <vector>

#include "VulkanError.hpp"
#include "renderer/DescriptorSets.hpp"
#include "renderer/MaterialTextureSlots.hpp"
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::renderer
{
namespace
{
constexpr auto textureDescriptorsPerDraw = static_cast<std::uint32_t>(materialTextureSlots.size());

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
}

MaterialTextureBindings::MaterialTextureBindings(const vk::raii::Device& device,
                                                 vk::PhysicalDevice physicalDevice,
                                                 float maxSamplerAnisotropy,
                                                 std::shared_ptr<TextureFilteringPreset> preset)
    : _device {device},
      _textureFilteringPreset {std::move(preset)},
      _maxSamplerAnisotropy {std::max(1.0F, maxSamplerAnisotropy)}
{
    auto features = vk::PhysicalDeviceFeatures2 {};
    physicalDevice.getFeatures2(&features);
    _samplerAnisotropySupported = features.features.samplerAnisotropy == vk::True;
}

auto MaterialTextureBindings::ensureSets(std::size_t index, std::size_t count) -> Result<void>
{
    auto& descriptors = _frames.at(index).descriptors;
    if (count <= descriptors.sets.size())
    {
        return {};
    }
    return createDescriptorSets(_device, _layout, _bindings, count).transform([&](auto replacement) {
        descriptors.sets.clear();
        descriptors = std::move(replacement);
    });
}

auto MaterialTextureBindings::getSet(std::size_t index, std::size_t drawIndex) const -> vk::DescriptorSet
{
    return *_frames.at(index).descriptors.sets.at(drawIndex);
}

auto MaterialTextureBindings::initialize(vk::DescriptorSetLayout layout,
                                         std::vector<vk::DescriptorSetLayoutBinding> bindings,
                                         std::size_t frameCount) -> std::expected<void, Error>
{
    _frames.resize(frameCount);
    _layout = layout;
    _bindings = std::move(bindings);
    return detail::createVulkanSampler(_device,
                                       vk::SamplerCreateInfo {.magFilter = vk::Filter::eLinear,
                                                              .minFilter = vk::Filter::eLinear,
                                                              .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                              .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                              .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                              .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                              .maxAnisotropy = 1.0F,
                                                              .minLod = 0.0F,
                                                              .maxLod = 0.0F})
        .transform([this](auto sampler) {
            _textureSampler = std::move(sampler);
        });
}

auto MaterialTextureBindings::createTextureSampler(TextureAddressMode addressMode,
                                                   std::uint32_t mipLevels,
                                                   TextureFilteringPreset preset,
                                                   float anisotropy) const -> std::expected<vk::raii::Sampler, Error>
{
    const auto filtering = mapTextureFilter(preset);
    return detail::createVulkanSampler(
        _device,
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

auto MaterialTextureBindings::getTextureSampler(FrameResources& frame, const Texture& texture)
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

auto MaterialTextureBindings::getOrCreateTextureSampler(FrameResources& frame, const TextureSamplerKey& key)
    -> std::expected<vk::Sampler, Error>
{
    const auto cached = std::ranges::find_if(frame.textureSamplers, [&](const TextureSampler& sampler) {
        return sampler.key == key;
    });
    if (cached != frame.textureSamplers.end())
    {
        cached->used = true;
        return *cached->sampler;
    }
    return createTextureSampler(key.addressMode, key.mipLevels, key.preset, key.anisotropy)
        .transform([&](auto created) {
            frame.textureSamplers.push_back(TextureSampler {.key = key, .sampler = std::move(created), .used = true});
            return *frame.textureSamplers.back().sampler;
        });
}

auto MaterialTextureBindings::write(std::size_t frameIndex, const RenderView& view, vk::ImageView textureFallbackView)
    -> std::expected<void, Error>
{
    auto& frame = _frames.at(frameIndex);
    auto imageInfos = std::vector<vk::DescriptorImageInfo>(view.meshes.size() * textureDescriptorsPerDraw);
    auto writes = std::vector<vk::WriteDescriptorSet> {};
    writes.reserve(imageInfos.size());
    std::ranges::for_each(frame.textureSamplers, [](auto& sampler) {
        sampler.used = false;
    });
    auto batch = TextureDescriptorWriteBatch {.frame = frame,
                                              .fallbackView = textureFallbackView,
                                              .imageInfos = imageInfos,
                                              .writes = writes};
    for (auto index = std::size_t {}; index < view.meshes.size(); ++index)
    {
        if (const auto appended = appendTextureDescriptorWrites(batch, index, view.meshes.at(index)); !appended)
        {
            return appended;
        }
    }
    _device.updateDescriptorSets(writes, {});
    std::erase_if(frame.textureSamplers, [](const TextureSampler& sampler) {
        return !sampler.used;
    });
    return {};
}

auto MaterialTextureBindings::appendTextureDescriptorWrites(TextureDescriptorWriteBatch& batch,
                                                            std::size_t drawIndex,
                                                            const MeshDraw& draw) -> std::expected<void, Error>
{
    const auto descriptorSet = *batch.frame.descriptors.sets.at(drawIndex);
    for (auto slotIndex = std::size_t {}; slotIndex < materialTextureSlots.size(); ++slotIndex)
    {
        const auto& slot = materialTextureSlots.at(slotIndex);
        const auto imageInfoIndex = (drawIndex * textureDescriptorsPerDraw) + slotIndex;
        const auto& texture = draw.material.*(slot.texture);
        auto sampler = getTextureSampler(batch.frame, texture);
        if (!sampler)
        {
            return std::unexpected {sampler.error()};
        }
        batch.imageInfos.at(imageInfoIndex) = makeTextureDescriptorInfo(texture, *sampler, batch.fallbackView);
        const auto& binding = _bindings.at(slotIndex);
        batch.writes.push_back({.dstSet = descriptorSet,
                                .dstBinding = binding.binding,
                                .descriptorCount = binding.descriptorCount,
                                .descriptorType = binding.descriptorType,
                                .pImageInfo = &batch.imageInfos.at(imageInfoIndex)});
    }
    return {};
}

auto MaterialTextureBindings::getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float
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

auto MaterialTextureBindings::requestedTextureFilteringPreset() const noexcept -> TextureFilteringPreset
{
    return _textureFilteringPreset ? *_textureFilteringPreset : TextureFilteringPreset::Anisotropic8x;
}
}

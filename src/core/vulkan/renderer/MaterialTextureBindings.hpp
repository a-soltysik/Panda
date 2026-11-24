#pragma once

#include <memory>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>

#include "renderer/DescriptorSets.hpp"

namespace panda::core::vulkan::renderer
{
class MaterialTextureBindings final
{
public:
    MaterialTextureBindings(const vk::raii::Device& device,
                            vk::PhysicalDevice physicalDevice,
                            float maxSamplerAnisotropy,
                            std::shared_ptr<TextureFilteringPreset> preset);
    [[nodiscard]] auto initialize(vk::DescriptorSetLayout layout,
                                  std::vector<vk::DescriptorSetLayoutBinding> bindings,
                                  std::size_t frameCount) -> Result<void>;
    [[nodiscard]] auto ensureSets(std::size_t index, std::size_t count) -> Result<void>;
    [[nodiscard]] auto write(std::size_t frameIndex, const RenderView& view, vk::ImageView textureFallbackView)
        -> Result<void>;
    [[nodiscard]] auto getSet(std::size_t index, std::size_t drawIndex) const -> vk::DescriptorSet;
    [[nodiscard]] auto getTextureSampler(std::size_t frameIndex, const Texture& texture)
        -> std::expected<vk::Sampler, Error>;
    [[nodiscard]] auto getEffectiveAnisotropy(TextureFilteringPreset preset) const noexcept -> float;

private:
    struct TextureSamplerKey
    {
        TextureAddressMode addressMode;
        TextureFilteringPreset preset;
        std::uint32_t mipLevels;
        float anisotropy;
        friend auto operator==(const TextureSamplerKey&, const TextureSamplerKey&) -> bool = default;
    };

    struct TextureSampler
    {
        TextureSamplerKey key;
        vk::raii::Sampler sampler {nullptr};
        bool used {false};
    };

    struct FrameResources
    {
        DescriptorSets descriptors;
        std::vector<TextureSampler> textureSamplers;
    };

    struct TextureDescriptorWriteBatch
    {
        FrameResources& frame;
        vk::ImageView fallbackView;
        std::vector<vk::DescriptorImageInfo>& imageInfos;
        std::vector<vk::WriteDescriptorSet>& writes;
    };

    [[nodiscard]] auto createTextureSampler(TextureAddressMode addressMode,
                                            std::uint32_t mipLevels,
                                            TextureFilteringPreset preset,
                                            float anisotropy) const -> std::expected<vk::raii::Sampler, Error>;
    [[nodiscard]] auto getTextureSampler(FrameResources& frame, const Texture& texture)
        -> std::expected<vk::Sampler, Error>;
    [[nodiscard]] auto getOrCreateTextureSampler(FrameResources& frame, const TextureSamplerKey& key)
        -> std::expected<vk::Sampler, Error>;
    [[nodiscard]] auto requestedTextureFilteringPreset() const noexcept -> TextureFilteringPreset;
    [[nodiscard]] auto appendTextureDescriptorWrites(TextureDescriptorWriteBatch& batch,
                                                     std::size_t drawIndex,
                                                     const MeshDraw& draw) -> std::expected<void, Error>;
    const vk::raii::Device& _device;
    std::shared_ptr<TextureFilteringPreset> _textureFilteringPreset;
    bool _samplerAnisotropySupported {};
    float _maxSamplerAnisotropy;
    vk::DescriptorSetLayout _layout;
    std::vector<vk::DescriptorSetLayoutBinding> _bindings;
    vk::raii::Sampler _textureSampler {nullptr};
    std::vector<FrameResources> _frames;
};
}

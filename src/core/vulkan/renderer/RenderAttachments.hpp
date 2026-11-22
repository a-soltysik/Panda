#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <vector>

#include "renderer/ShadowAtlas.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class TextureResource;
}

namespace panda::core::vulkan::renderer
{
inline constexpr auto shadowExtent = vk::Extent2D {.width = shadowAtlasExtent, .height = shadowAtlasExtent};
inline constexpr auto hdrColorFormat = vk::Format::eR16G16B16A16Sfloat;

struct OutputDescription
{
    vk::Extent2D extent {};
    vk::Format colorFormat {vk::Format::eUndefined};
    vk::Format depthFormat {vk::Format::eUndefined};
    vk::SampleCountFlagBits sampleCount {vk::SampleCountFlagBits::e1};
    SampleCount requestedSampleCount {SampleCount::Four};
};

struct RenderAttachmentsCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    vk::Extent2D extent;
    vk::Format colorFormat;
    std::size_t frameCount;
    SampleCount requestedSampleCount {SampleCount::Four};
};

class RenderAttachments final
{
public:
    struct PendingResize
    {
        OutputDescription description;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> colorAttachments;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> resolvedColorAttachments;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> depthAttachments;
    };

    [[nodiscard]] static auto create(const RenderAttachmentsCreateInfo& info)
        -> std::expected<std::unique_ptr<RenderAttachments>, Error>;
    [[nodiscard]] static auto getSupportedSampleCounts(vk::PhysicalDevice physicalDevice, vk::Format colorFormat)
        -> Result<std::vector<SampleCount>>;
    [[nodiscard]] static auto validateRequiredFormats(vk::PhysicalDevice physicalDevice) -> Result<void>;

    RenderAttachments(const RenderAttachments&) = delete;
    auto operator=(const RenderAttachments&) -> RenderAttachments& = delete;
    RenderAttachments(RenderAttachments&&) = delete;
    auto operator=(RenderAttachments&&) -> RenderAttachments& = delete;
    ~RenderAttachments();

    [[nodiscard]] auto getOutputDescription() const noexcept -> const OutputDescription&;
    [[nodiscard]] auto getShadowView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto getShadowLayerView(std::size_t frameIndex, std::uint32_t layer) const -> vk::ImageView;
    [[nodiscard]] auto getDepthView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto getColorView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto getResolvedColorView(std::size_t frameIndex) const -> vk::ImageView;
    void transitionResolvedColorToAttachment(vk::CommandBuffer command, std::size_t frameIndex) const;
    void transitionResolvedColorToSampling(vk::CommandBuffer command, std::size_t frameIndex) const;
    [[nodiscard]] auto prepareResize(vk::Extent2D extent,
                                     vk::Format colorFormat,
                                     SampleCount requestedSampleCount = SampleCount::Four)
        -> std::expected<PendingResize, Error>;
    [[nodiscard]] auto ensureShadowAtlasLayers(std::size_t frameIndex, std::uint32_t layerCount)
        -> std::expected<bool, Error>;
    void publishResize(PendingResize resize) noexcept;
    void transitionColor(vk::CommandBuffer command, std::size_t frameIndex) const;
    void transitionDepth(vk::CommandBuffer command, std::size_t frameIndex) const;
    void transitionShadowForAttachment(vk::CommandBuffer command, std::size_t frameIndex) const;
    void transitionShadowForSampling(vk::CommandBuffer command, std::size_t frameIndex) const;
    void markSubmitted(std::size_t frameIndex) noexcept;

private:
    struct FrameResources
    {
        std::shared_ptr<::panda::detail::TextureResource> shadowMap;
        std::shared_ptr<::panda::detail::TextureResource> colorAttachment;
        std::shared_ptr<::panda::detail::TextureResource> resolvedColorAttachment;
        std::shared_ptr<::panda::detail::TextureResource> depthAttachment;
        bool colorInitialized {false};
        bool resolvedColorInitialized {false};
        bool depthInitialized {false};
        bool shadowInitialized {false};
    };

    RenderAttachments(vk::PhysicalDevice physicalDevice,
                      const vk::raii::Device& device,
                      resources::MemoryPool& pool,
                      std::size_t frameCount);

    [[nodiscard]] auto initialize(const RenderAttachmentsCreateInfo& info) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowMaps() -> std::expected<void, Error>;
    [[nodiscard]] auto createDepthAttachments(vk::Extent2D extent, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createColorAttachments(vk::Extent2D extent, vk::Format format, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createResolvedColorAttachments(vk::Extent2D extent)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createShadowMaps(std::uint32_t layerCount)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createResizeAttachments(const OutputDescription& description,
                                               bool extentChanged,
                                               bool formatChanged) -> std::expected<PendingResize, Error>;
    void replaceDepthAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept;
    void replaceColorAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept;
    void replaceResolvedColorAttachments(
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    std::vector<FrameResources> _frames;
    OutputDescription _description;
};
}

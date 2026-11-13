#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
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

struct OutputDescription
{
    vk::Extent2D extent {};
    vk::Format colorFormat {vk::Format::eUndefined};
    vk::Format depthFormat {vk::Format::eUndefined};
    vk::SampleCountFlagBits sampleCount {vk::SampleCountFlagBits::e1};
};

struct RenderAttachmentsCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    vk::Extent2D extent;
    vk::Format colorFormat;
    std::size_t frameCount;
};

class RenderAttachments final
{
public:
    struct PendingResize
    {
        OutputDescription description;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> colorAttachments;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> depthAttachments;
    };

    [[nodiscard]] static auto create(const RenderAttachmentsCreateInfo& info)
        -> std::expected<std::unique_ptr<RenderAttachments>, Error>;

    RenderAttachments(const RenderAttachments&) = delete;
    auto operator=(const RenderAttachments&) -> RenderAttachments& = delete;
    RenderAttachments(RenderAttachments&&) = delete;
    auto operator=(RenderAttachments&&) -> RenderAttachments& = delete;
    ~RenderAttachments();

    [[nodiscard]] auto getOutputDescription() const noexcept -> const OutputDescription&;
    [[nodiscard]] auto getShadowView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto getDepthView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto getColorView(std::size_t frameIndex) const -> vk::ImageView;
    [[nodiscard]] auto prepareResize(vk::Extent2D extent, vk::Format colorFormat)
        -> std::expected<PendingResize, Error>;
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
        std::shared_ptr<::panda::detail::TextureResource> depthAttachment;
        bool colorInitialized {false};
        bool depthInitialized {false};
        bool shadowInitialized {false};
    };

    RenderAttachments(vk::PhysicalDevice physicalDevice,
                      const vk::raii::Device& device,
                      resources::MemoryPool& pool,
                      std::size_t frameCount);

    [[nodiscard]] auto initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowMaps() -> std::expected<void, Error>;
    [[nodiscard]] auto createDepthAttachments(vk::Extent2D extent, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createColorAttachments(vk::Extent2D extent, vk::Format format, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createShadowMaps()
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createResizeAttachments(const OutputDescription& description,
                                               bool extentChanged,
                                               bool formatChanged) -> std::expected<PendingResize, Error>;
    void replaceDepthAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept;
    void replaceColorAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> replacements) noexcept;
    [[nodiscard]] auto chooseDepthFormat() const -> std::expected<vk::Format, Error>;
    [[nodiscard]] auto chooseSampleCount(vk::Format colorFormat) const -> std::expected<vk::SampleCountFlagBits, Error>;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    std::vector<FrameResources> _frames;
    OutputDescription _description;
};
}

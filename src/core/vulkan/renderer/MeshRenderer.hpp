#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <vector>

#include "renderer/FrameBindings.hpp"
#include "renderer/MeshPipelineCache.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class TextureResource;
}

namespace panda::core::vulkan::renderer
{
struct MeshRendererCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    vk::Extent2D extent;
    vk::Format colorFormat;
    std::size_t frameCount;
};

class MeshRenderer final
{
public:
    [[nodiscard]] static auto create(const MeshRendererCreateInfo& info)
        -> std::expected<std::unique_ptr<MeshRenderer>, Error>;

    MeshRenderer(const MeshRenderer&) = delete;
    auto operator=(const MeshRenderer&) -> MeshRenderer& = delete;
    MeshRenderer(MeshRenderer&&) = delete;
    auto operator=(MeshRenderer&&) -> MeshRenderer& = delete;
    ~MeshRenderer();

    [[nodiscard]] auto resize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto updateFrame(std::size_t frameIndex, const RenderView& view) -> std::expected<void, Error>;
    [[nodiscard]] auto getSampleCount() const noexcept -> vk::SampleCountFlagBits;
    void record(vk::CommandBuffer command, vk::ImageView colorView, std::size_t frameIndex, const RenderView& view);
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

    struct ResizeResources
    {
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> colorAttachments;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> depthAttachments;
    };

    struct ResizeRequest
    {
        vk::Extent2D extent {};
        vk::Format colorFormat {vk::Format::eUndefined};
        vk::SampleCountFlagBits sampleCount {vk::SampleCountFlagBits::e1};
        bool extentChanged {false};
        bool formatChanged {false};
        bool sampleCountChanged {false};
    };

    struct BlendDraw
    {
        const MeshDraw* draw {};
        std::uint32_t materialIndex {};
        float cameraDepth {};
    };

    struct DrawPass
    {
        bool shadowPass {false};
        std::uint32_t materialIndex {};
    };

    explicit MeshRenderer(const MeshRendererCreateInfo& info);

    [[nodiscard]] auto pipelineCache() -> MeshPipelineCache&;
    [[nodiscard]] auto pipelineCache() const -> const MeshPipelineCache&;
    [[nodiscard]] auto initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat)
        -> std::expected<void, Error>;
    [[nodiscard]] auto initializeAttachments(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeBindingsAndPipeline() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowMaps() -> std::expected<void, Error>;
    [[nodiscard]] auto createDepthAttachments(vk::Extent2D extent, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createColorAttachments(vk::Extent2D extent, vk::Format format, vk::SampleCountFlagBits samples)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createShadowMaps()
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createResizeResources(const ResizeRequest& request) -> std::expected<ResizeResources, Error>;
    [[nodiscard]] auto createDepthAttachmentsForResize(const ResizeRequest& request)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createColorAttachmentsForResize(const ResizeRequest& request)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto chooseDepthFormat() const -> std::expected<vk::Format, Error>;
    [[nodiscard]] auto chooseSampleCount(vk::Format colorFormat) const -> std::expected<vk::SampleCountFlagBits, Error>;
    void publishResize(const ResizeRequest& request, ResizeResources resources);
    void publishDepthAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments);
    void publishColorAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments);
    void transitionColor(vk::CommandBuffer command, FrameResources& frame);
    void recordShadowPass(vk::CommandBuffer command,
                          FrameResources& frame,
                          std::size_t frameIndex,
                          const RenderView& view);
    void recordColorPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view);
    static void transitionDepth(vk::CommandBuffer command, FrameResources& frame);
    static void transitionShadowForAttachment(vk::CommandBuffer command, FrameResources& frame);
    static void transitionShadowForSampling(vk::CommandBuffer command, FrameResources& frame);
    void recordBlendedDraws(vk::CommandBuffer command, std::size_t frameIndex);
    void recordAttachments(vk::CommandBuffer command,
                           vk::ImageView swapchainView,
                           const FrameResources& frame,
                           vk::ImageView depthView) const;
    static void recordShadowAttachment(vk::CommandBuffer command, vk::ImageView depthView);
    void setDrawState(vk::CommandBuffer command, std::size_t frameIndex, vk::Extent2D extent) const;
    void bindTextureDescriptors(vk::CommandBuffer command,
                                std::size_t frameIndex,
                                const MeshDraw& draw,
                                DrawPass pass) const;
    void recordDraw(vk::CommandBuffer command, std::size_t frameIndex, const MeshDraw& draw, DrawPass pass) const;
    static void recordMeshDraw(vk::CommandBuffer command,
                               const ::panda::detail::MeshResource& mesh,
                               std::uint32_t materialIndex);

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    std::vector<FrameResources> _frames;
    std::unique_ptr<FrameBindings> _bindings;
    std::optional<MeshPipelineCache> _pipelineCache;
    std::vector<BlendDraw> _blendDraws;
    vk::Format _colorFormat {vk::Format::eUndefined};
    vk::Format _depthFormat {vk::Format::eUndefined};
    vk::SampleCountFlagBits _sampleCount {vk::SampleCountFlagBits::e1};
    vk::Extent2D _extent {};
};
}

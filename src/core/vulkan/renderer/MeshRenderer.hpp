#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <vector>

#include "renderer/FrameBindings.hpp"
#include "renderer/MeshPipelineCache.hpp"
#include "renderer/RenderAttachments.hpp"
#include "renderer/ShaderData.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class TextureResource;
}

namespace panda::core::vulkan::renderer
{
[[nodiscard]] auto makeClearColor(vk::Format format) -> vk::ClearColorValue;

struct MeshRendererCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    resources::MemoryPool& pool;
    vk::Extent2D extent;
    vk::Format colorFormat;
    std::size_t frameCount;
    std::uint32_t maxLights {::panda::ContextOptions::defaultMaxLights};
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
    struct FrameDescriptorBinding
    {
        std::size_t frameIndex;
        std::uint32_t shadowViewOffset;
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
    [[nodiscard]] auto initializeAttachments(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeBindingsAndPipeline() -> std::expected<void, Error>;
    void recordShadowPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view);
    void recordShadowView(vk::CommandBuffer command,
                          std::size_t frameIndex,
                          const RenderView& view,
                          std::size_t shadowViewIndex);
    void recordColorPass(vk::CommandBuffer command, std::size_t frameIndex, const RenderView& view);
    void recordBlendedDraws(vk::CommandBuffer command, std::size_t frameIndex);
    void recordAttachments(vk::CommandBuffer command, vk::ImageView swapchainView, std::size_t frameIndex) const;
    static void recordShadowAttachment(vk::CommandBuffer command, vk::ImageView depthView);
    static void setViewportAndScissor(vk::CommandBuffer command, vk::Extent2D extent, vk::Offset2D offset = {});
    void bindFrameDescriptorSet(vk::CommandBuffer command, FrameDescriptorBinding binding) const;
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
    std::size_t _frameCount;
    std::uint32_t _maxLights;
    std::unique_ptr<RenderAttachments> _attachments;
    std::unique_ptr<FrameBindings> _bindings;
    std::optional<MeshPipelineCache> _pipelineCache;
    std::vector<BlendDraw> _blendDraws;
};
}

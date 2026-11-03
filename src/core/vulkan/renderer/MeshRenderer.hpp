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
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <vector>

#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class BufferResource;
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
        std::shared_ptr<::panda::detail::BufferResource> uniformBuffer;
        std::shared_ptr<::panda::detail::TextureResource> colorAttachment;
        std::shared_ptr<::panda::detail::TextureResource> depthAttachment;
        std::shared_ptr<::panda::detail::TextureResource> shadowMap;
        bool colorInitialized {false};
        bool depthInitialized {false};
        bool shadowInitialized {false};
        vk::raii::DescriptorSet descriptorSet {nullptr};
    };

    struct ResizeResources
    {
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> colorAttachments;
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> depthAttachments;
        std::optional<vk::raii::Pipeline> pipeline;
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

    explicit MeshRenderer(const MeshRendererCreateInfo& info);

    [[nodiscard]] auto initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorState() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorSetLayout() -> std::expected<void, Error>;
    [[nodiscard]] auto initializePipelineLayout() -> std::expected<void, Error>;
    [[nodiscard]] auto createDescriptorPool() -> std::expected<void, Error>;
    [[nodiscard]] auto allocateDescriptorSets() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameBuffers() -> std::expected<void, Error>;
    void updateDescriptorSet(const FrameResources& frame);
    [[nodiscard]] auto initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat)
        -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowResources() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowMaps() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeShadowSampler() -> std::expected<void, Error>;
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
    [[nodiscard]] auto createPipelineForResize(const ResizeRequest& request)
        -> std::expected<std::optional<vk::raii::Pipeline>, Error>;
    [[nodiscard]] auto chooseDepthFormat() const -> std::expected<vk::Format, Error>;
    [[nodiscard]] auto chooseSampleCount(vk::Format colorFormat) const -> std::expected<vk::SampleCountFlagBits, Error>;
    [[nodiscard]] auto createPipeline(vk::Format colorFormat, vk::SampleCountFlagBits samples) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createShadowPipeline() const -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createGraphicsPipeline(vk::Format colorFormat,
                                              vk::SampleCountFlagBits samples,
                                              vk::ShaderModule vertex,
                                              vk::ShaderModule fragment) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto makeGraphicsPipelineCreateInfo(std::span<const vk::PipelineShaderStageCreateInfo> stages,
                                                      const vk::PipelineRenderingCreateInfo& rendering,
                                                      const vk::PipelineMultisampleStateCreateInfo& multisampling,
                                                      const vk::PipelineColorBlendStateCreateInfo& colorBlend) const
        -> vk::GraphicsPipelineCreateInfo;
    [[nodiscard]] auto createShaderModule(std::span<const std::uint32_t> words) const
        -> std::expected<vk::raii::ShaderModule, Error>;
    void publishResize(const ResizeRequest& request, ResizeResources resources);
    void publishDepthAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments);
    void publishColorAttachments(std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments);
    void transitionColor(vk::CommandBuffer command, FrameResources& frame);
    static void transitionDepth(vk::CommandBuffer command, FrameResources& frame);
    static void transitionShadowForAttachment(vk::CommandBuffer command, FrameResources& frame);
    static void transitionShadowForSampling(vk::CommandBuffer command, FrameResources& frame);
    void recordAttachments(vk::CommandBuffer command,
                           vk::ImageView swapchainView,
                           const FrameResources& frame,
                           vk::ImageView depthView) const;
    static void recordShadowAttachment(vk::CommandBuffer command, vk::ImageView depthView);
    void setDrawState(vk::CommandBuffer command,
                      const FrameResources& frame,
                      vk::Pipeline pipeline,
                      vk::Extent2D extent) const;
    void recordDraw(vk::CommandBuffer command, const MeshDraw& draw) const;
    static void recordMeshDraw(vk::CommandBuffer command, const ::panda::detail::MeshResource& mesh);

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    vk::raii::DescriptorPool _descriptorPool {nullptr};
    vk::raii::Sampler _shadowSampler {nullptr};
    std::vector<FrameResources> _frames;
    vk::raii::Pipeline _pipeline {nullptr};
    vk::raii::Pipeline _shadowPipeline {nullptr};
    vk::Format _colorFormat {vk::Format::eUndefined};
    vk::Format _depthFormat {vk::Format::eUndefined};
    vk::SampleCountFlagBits _sampleCount {vk::SampleCountFlagBits::e1};
    vk::Extent2D _extent {};
};
}

#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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
    void record(vk::CommandBuffer command, vk::ImageView colorView, std::size_t frameIndex, const RenderView& view);
    void markSubmitted(std::size_t frameIndex) noexcept;

private:
    struct FrameResources
    {
        std::shared_ptr<::panda::detail::BufferResource> uniformBuffer;
        vk::raii::DescriptorSet descriptorSet {nullptr};
        std::shared_ptr<::panda::detail::TextureResource> depthAttachment;
        bool depthInitialized {false};
    };

    struct ResizeResources
    {
        std::vector<std::shared_ptr<::panda::detail::TextureResource>> depthAttachments;
        vk::raii::Pipeline pipeline {nullptr};
    };

    explicit MeshRenderer(const MeshRendererCreateInfo& info);

    [[nodiscard]] auto initialize(vk::Extent2D extent, vk::Format colorFormat) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptorState() -> std::expected<void, Error>;
    [[nodiscard]] auto createDescriptorPool() -> std::expected<void, Error>;
    [[nodiscard]] auto allocateDescriptorSets() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameBuffers() -> std::expected<void, Error>;
    void updateDescriptorSet(const FrameResources& frame);
    [[nodiscard]] auto initializeDepthAndPipeline(vk::Extent2D extent, vk::Format colorFormat)
        -> std::expected<void, Error>;
    [[nodiscard]] auto createDepthAttachments(vk::Extent2D extent)
        -> std::expected<std::vector<std::shared_ptr<::panda::detail::TextureResource>>, Error>;
    [[nodiscard]] auto createResizeResources(vk::Extent2D extent,
                                             vk::Format colorFormat,
                                             bool extentChanged,
                                             bool formatChanged) -> std::expected<ResizeResources, Error>;
    [[nodiscard]] auto chooseDepthFormat() const -> std::expected<vk::Format, Error>;
    [[nodiscard]] auto createPipeline(vk::Format colorFormat) const -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createGraphicsPipeline(vk::Format colorFormat,
                                              vk::ShaderModule vertex,
                                              vk::ShaderModule fragment) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createShaderModule(std::span<const std::uint32_t> words) const
        -> std::expected<vk::raii::ShaderModule, Error>;
    void publishResize(vk::Extent2D extent,
                       vk::Format colorFormat,
                       std::vector<std::shared_ptr<::panda::detail::TextureResource>> attachments,
                       vk::raii::Pipeline pipeline);
    static void transitionDepth(vk::CommandBuffer command, FrameResources& frame);
    void recordAttachments(vk::CommandBuffer command, vk::ImageView colorView, vk::ImageView depthView) const;
    void setDrawState(vk::CommandBuffer command, const FrameResources& frame) const;
    void recordDraw(vk::CommandBuffer command, const MeshDraw& draw) const;
    static void recordMeshDraw(vk::CommandBuffer command, const ::panda::detail::MeshResource& mesh);

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    resources::MemoryPool& _pool;
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    vk::raii::DescriptorPool _descriptorPool {nullptr};
    std::vector<FrameResources> _frames;
    vk::raii::Pipeline _pipeline {nullptr};
    vk::Format _colorFormat {vk::Format::eUndefined};
    vk::Format _depthFormat {vk::Format::eUndefined};
    vk::Extent2D _extent {};
};
}

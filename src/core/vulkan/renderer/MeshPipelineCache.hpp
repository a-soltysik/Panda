#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <compare>
#include <cstdint>
#include <expected>
#include <map>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <span>
#include <vector>

namespace panda::core::vulkan::renderer
{
class MeshPipelineCache final
{
public:
    struct MeshPipelineKey
    {
        bool lit {false};
        bool hasColor {false};
        bool hasTextureCoordinates {false};
        std::uint32_t vertexStride {0};
        std::uint32_t positionOffset {0};
        std::uint32_t normalOffset {0};
        std::uint32_t texCoordOffset {0};
        std::uint32_t colorOffset {0};
        RenderFace renderFace {RenderFace::Front};
        AlphaMode alphaMode {AlphaMode::Opaque};
        bool mirrored {false};
        friend auto operator<=>(const MeshPipelineKey&, const MeshPipelineKey&) = default;
    };

    struct ShadowPipelineKey
    {
        std::uint32_t vertexStride {0};
        std::uint32_t positionOffset {0};
        std::uint32_t texCoordOffset {0};
        std::uint32_t colorOffset {0};
        bool hasColor {false};
        bool hasBaseColorTexture {false};
        RenderFace renderFace {RenderFace::Front};
        AlphaMode alphaMode {AlphaMode::Opaque};
        bool mirrored {false};
        friend auto operator<=>(const ShadowPipelineKey&, const ShadowPipelineKey&) = default;
    };

    struct OutputCompatibility
    {
        vk::Format colorFormat {vk::Format::eUndefined};
        vk::Format depthFormat {vk::Format::eUndefined};
        vk::SampleCountFlagBits sampleCount {vk::SampleCountFlagBits::e1};
    };

    MeshPipelineCache(const vk::raii::Device& device, vk::PipelineLayout pipelineLayout);

    [[nodiscard]] static auto makeMeshPipelineKey(const MeshDraw& draw) -> MeshPipelineKey;
    [[nodiscard]] static auto makeShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey;
    void setOutputCompatibility(OutputCompatibility compatibility);
    [[nodiscard]] auto prepare(const RenderView& view) -> std::expected<void, Error>;
    [[nodiscard]] auto prepare(const MeshPipelineKey& key) -> std::expected<void, Error>;
    [[nodiscard]] auto prepare(const ShadowPipelineKey& key) -> std::expected<void, Error>;
    [[nodiscard]] auto getPipeline(const MeshDraw& draw) const -> vk::Pipeline;
    [[nodiscard]] auto getShadowPipeline(const MeshDraw& draw) const -> vk::Pipeline;

private:
    [[nodiscard]] auto createPipeline(const MeshPipelineKey& key) const -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createMeshPipeline(const MeshPipelineKey& key,
                                          vk::ShaderModule vertex,
                                          vk::ShaderModule fragment) const -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createShadowPipeline(const ShadowPipelineKey& key) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto prepareDraw(const MeshDraw& draw) -> std::expected<void, Error>;
    [[nodiscard]] static auto makeVertexAttributeDescriptions(const MeshPipelineKey& key)
        -> std::vector<vk::VertexInputAttributeDescription>;

    const vk::raii::Device& _device;
    vk::PipelineLayout _pipelineLayout;
    OutputCompatibility _outputCompatibility;
    std::map<MeshPipelineKey, vk::raii::Pipeline> _pipelines;
    std::map<ShadowPipelineKey, vk::raii::Pipeline> _shadowPipelines;
};
}

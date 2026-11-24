#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <span>
#include <tuple>
#include <vector>

#include "renderer/ShaderReflection.hpp"

namespace panda::core::vulkan::renderer
{
class ApplicationShaderBindings;

struct ShadowShaderModules
{
    vk::raii::ShaderModule vertex {nullptr};
    std::optional<vk::raii::ShaderModule> fragment;
};

class MeshPipelineCache final
{
public:
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    // These cache-key records remain aggregates for concise designated construction.
    struct MeshPipelineKey
    {
        bool lit {false};
        bool hasColor {false};
        bool hasTextureCoordinates {false};
        bool hasNormalMap {false};
        std::uint32_t vertexStride {0};
        std::uint32_t positionOffset {0};
        std::uint32_t normalOffset {0};
        std::uint32_t texCoordOffset {0};
        std::uint32_t tangentOffset {0};
        std::uint32_t colorOffset {0};
        RenderFace renderFace {RenderFace::Front};
        AlphaMode alphaMode {AlphaMode::Opaque};
        bool mirrored {false};
        bool hasEnvironment {false};
        bool procedural {false};
        std::uint64_t customProgramRevision {};
        std::shared_ptr<const ShaderProgram::State> customProgram;

        [[nodiscard]] auto order() const
        {
            return std::tie(lit,
                            hasColor,
                            hasTextureCoordinates,
                            hasNormalMap,
                            vertexStride,
                            positionOffset,
                            normalOffset,
                            texCoordOffset,
                            tangentOffset,
                            colorOffset,
                            renderFace,
                            alphaMode,
                            mirrored,
                            hasEnvironment,
                            procedural,
                            customProgramRevision);
        }

        friend auto operator<=>(const MeshPipelineKey& first, const MeshPipelineKey& second)
        {
            return first.order() <=> second.order();
        }

        friend auto operator==(const MeshPipelineKey& first, const MeshPipelineKey& second) -> bool
        {
            return first.order() == second.order();
        }
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
        bool hasNormal {false};
        bool hasTangent {false};
        bool hasTextureCoordinates {false};
        bool procedural {false};
        std::uint32_t normalOffset {0};
        std::uint32_t tangentOffset {0};
        std::uint64_t customProgramRevision {};
        std::shared_ptr<const ShaderProgram::State> customProgram;

        [[nodiscard]] auto order() const
        {
            return std::tie(vertexStride,
                            positionOffset,
                            texCoordOffset,
                            colorOffset,
                            hasColor,
                            hasBaseColorTexture,
                            renderFace,
                            alphaMode,
                            mirrored,
                            hasNormal,
                            hasTangent,
                            hasTextureCoordinates,
                            procedural,
                            normalOffset,
                            tangentOffset,
                            customProgramRevision);
        }

        friend auto operator<=>(const ShadowPipelineKey& first, const ShadowPipelineKey& second)
        {
            return first.order() <=> second.order();
        }

        friend auto operator==(const ShadowPipelineKey& first, const ShadowPipelineKey& second) -> bool
        {
            return first.order() == second.order();
        }
    };

    // NOLINTEND(misc-non-private-member-variables-in-classes)

    struct OutputCompatibility
    {
        vk::Format colorFormat {vk::Format::eUndefined};
        vk::Format depthFormat {vk::Format::eUndefined};
        vk::SampleCountFlagBits sampleCount {vk::SampleCountFlagBits::e1};
    };

    MeshPipelineCache(const vk::raii::Device& device,
                      vk::PipelineLayout pipelineLayout,
                      std::span<const ShaderDescriptor> shaderDescriptorSchema,
                      const ApplicationShaderBindings* applicationBindings = nullptr);

    [[nodiscard]] static auto makeMeshPipelineKey(const MeshDraw& draw, bool hasEnvironment = false) -> MeshPipelineKey;
    [[nodiscard]] static auto makeProceduralPipelineKey(const ProceduralDraw& draw) -> MeshPipelineKey;
    [[nodiscard]] static auto makeShadowPipelineKey(const MeshDraw& draw) -> ShadowPipelineKey;
    [[nodiscard]] static auto makeShadowPipelineKey(const ProceduralDraw& draw) -> ShadowPipelineKey;
    void setOutputCompatibility(OutputCompatibility compatibility);
    [[nodiscard]] auto prepare(const RenderView& view) -> std::expected<void, Error>;
    [[nodiscard]] auto prepare(const MeshPipelineKey& key) -> std::expected<void, Error>;
    [[nodiscard]] auto prepare(const ShadowPipelineKey& key) -> std::expected<void, Error>;
    [[nodiscard]] auto getPipeline(const MeshDraw& draw, bool hasEnvironment = false) const -> vk::Pipeline;
    [[nodiscard]] auto getPipeline(const ProceduralDraw& draw) const -> vk::Pipeline;
    [[nodiscard]] auto getShadowPipeline(const MeshDraw& draw) const -> vk::Pipeline;
    [[nodiscard]] auto getShadowPipeline(const ProceduralDraw& draw) const -> vk::Pipeline;
    [[nodiscard]] auto getSkyboxPipeline() const -> vk::Pipeline;

private:
    [[nodiscard]] auto prepareSkybox() -> std::expected<void, Error>;
    [[nodiscard]] auto createSkyboxPipeline() -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createSkyboxPipelineWithModules(vk::ShaderModule vertex, vk::ShaderModule fragment) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createPipeline(const MeshPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createBuiltinPipeline(const MeshPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createCustomPipeline(const MeshPipelineKey& key) const -> Result<vk::raii::Pipeline>;
    [[nodiscard]] auto createMeshPipeline(const MeshPipelineKey& key,
                                          const std::array<vk::ShaderModule, 2>& modules,
                                          std::span<const vk::VertexInputAttributeDescription> attributes,
                                          std::optional<std::uint32_t> alphaModeSpecializationId) const
        -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createShadowPipeline(const ShadowPipelineKey& key) -> std::expected<vk::raii::Pipeline, Error>;
    [[nodiscard]] auto createCustomShadowPipeline(const ShadowPipelineKey& key) const -> Result<vk::raii::Pipeline>;
    [[nodiscard]] auto createCustomShadowPipelineWithModules(
        const ShadowPipelineKey& key,
        std::span<const vk::VertexInputAttributeDescription> attributes,
        const ShadowShaderModules& modules) const -> Result<vk::raii::Pipeline>;
    [[nodiscard]] static auto createCustomMeshShadowAttributes(const ShadowPipelineKey& key,
                                                               std::span<const ShaderInterfaceVariable> vertexInputs)
        -> Result<std::vector<vk::VertexInputAttributeDescription>>;
    [[nodiscard]] auto createShadowPipelineWithModules(const ShadowPipelineKey& key,
                                                       const ShadowShaderModules& modules,
                                                       std::span<const vk::VertexInputAttributeDescription> attributes,
                                                       std::uint32_t specializationId) const
        -> Result<vk::raii::Pipeline>;
    [[nodiscard]] auto meshShaderLayout(const MeshPipelineKey& key)
        -> Result<std::reference_wrapper<const ShaderProgramLayout>>;
    [[nodiscard]] auto shadowShaderLayout(const ShadowPipelineKey& key)
        -> Result<std::reference_wrapper<const ShaderProgramLayout>>;
    [[nodiscard]] auto skyboxShaderLayout() -> Result<std::reference_wrapper<const ShaderProgramLayout>>;
    [[nodiscard]] auto prepareDraw(const MeshDraw& draw, bool hasEnvironment, bool hasShadowPass) -> Result<void>;
    void warnMissingNormalMapInputs(const MeshDraw& draw);
    [[nodiscard]] auto prepareCustomDraw(const MeshDraw& draw, bool hasShadowPass) -> Result<void>;
    [[nodiscard]] auto prepareCustomShadowDraw(const MeshDraw& draw, const MeshPipelineKey& camera, bool hasShadowPass)
        -> Result<void>;
    [[nodiscard]] auto prepareProceduralDraw(const ProceduralDraw& draw, bool hasShadowPass) -> Result<void>;
    [[nodiscard]] auto prepareMeshDraws(const RenderView& view, bool hasShadowPass) -> Result<void>;
    [[nodiscard]] auto prepareProceduralDraws(const RenderView& view, bool hasShadowPass) -> Result<void>;
    [[nodiscard]] static auto makeVertexAttributeDescriptions(const MeshPipelineKey& key,
                                                              std::span<const ShaderInterfaceVariable> vertexInputs)
        -> Result<std::vector<vk::VertexInputAttributeDescription>>;
    const vk::raii::Device& _device;
    vk::PipelineLayout _pipelineLayout;
    const ApplicationShaderBindings* _applicationBindings;
    std::vector<ShaderDescriptor> _shaderDescriptorSchema;
    std::map<std::array<bool, 5>, ShaderProgramLayout> _meshShaderLayouts;
    std::map<std::array<bool, 3>, ShaderProgramLayout> _shadowShaderLayouts;
    std::optional<ShaderProgramLayout> _skyboxShaderLayout;
    OutputCompatibility _outputCompatibility;
    std::map<MeshPipelineKey, vk::raii::Pipeline> _pipelines;
    std::map<ShadowPipelineKey, vk::raii::Pipeline> _shadowPipelines;
    std::optional<vk::raii::Pipeline> _skyboxPipeline;
    bool _warnedMissingNormalMapUv {false};
    bool _warnedMissingNormalMapTangent {false};
};
}

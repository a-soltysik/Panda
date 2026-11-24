#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <span>
#include <vector>

#include "renderer/DescriptorSets.hpp"
#include "renderer/FrameDescriptors.hpp"

namespace panda::detail
{
class BufferResource;
}

namespace panda::core::vulkan::resources
{
class MemoryPool;
}

namespace panda::core::vulkan::renderer
{
class FrameBindings;

class ApplicationShaderBindings final
{
public:
    struct DrawBindingIndex
    {
        std::size_t frameIndex;
        std::size_t drawIndex;
    };

    ApplicationShaderBindings(const vk::raii::Device& device,
                              FrameBindings& frameBindings,
                              resources::MemoryPool& pool,
                              std::size_t frameCount);

    [[nodiscard]] auto prepare(std::size_t frameIndex, const RenderView& view) -> Result<void>;
    [[nodiscard]] auto getPipelineLayout(const std::shared_ptr<const ShaderProgram::State>& program) const
        -> vk::PipelineLayout;
    void bind(vk::CommandBuffer command, DrawBindingIndex index) const;
    void bindForShadow(vk::CommandBuffer command, DrawBindingIndex index, const ShaderProgram& program) const;

private:
    struct SetLayout
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        vk::raii::DescriptorSetLayout layout {nullptr};
    };

    struct ProgramLayout
    {
        std::vector<SetLayout> sets;
        vk::raii::PipelineLayout pipeline {nullptr};
    };

    struct BoundResource
    {
        std::shared_ptr<::panda::detail::BufferResource> owner;
        Texture texture;
        vk::DescriptorBufferInfo bufferInfo {};
        vk::DescriptorImageInfo imageInfo {};
    };

    struct BoundSet
    {
        std::uint32_t index;
        std::vector<BoundResource> resources;
        DescriptorSets descriptors;
    };

    struct DrawBindings
    {
        vk::PipelineLayout pipeline;
        std::vector<BoundSet> sets;
    };

    [[nodiscard]] auto createLayout(const ShaderProgram::State& program) const -> Result<ProgramLayout>;
    [[nodiscard]] auto validateSetLayouts(std::span<const SetLayout> sets) const -> Result<void>;
    [[nodiscard]] static auto createSetLayouts(
        std::uint32_t firstSet,
        std::uint32_t lastSet,
        const std::map<std::pair<std::uint32_t, std::uint32_t>, vk::DescriptorSetLayoutBinding>& descriptors)
        -> Result<std::vector<SetLayout>>;
    [[nodiscard]] auto createNativeSetLayouts(ProgramLayout& layout) const
        -> Result<std::vector<vk::DescriptorSetLayout>>;
    [[nodiscard]] auto getOrCreateLayout(const std::shared_ptr<const ShaderProgram::State>& program)
        -> Result<const ProgramLayout*>;
    [[nodiscard]] auto prepareMeshDraws(std::size_t frameIndex,
                                        std::span<const MeshDraw> draws,
                                        std::map<std::size_t, DrawBindings>& bindings) -> Result<void>;
    [[nodiscard]] auto prepareProceduralDraws(std::size_t frameIndex,
                                              std::span<const ProceduralDraw> draws,
                                              std::size_t firstDrawIndex,
                                              std::map<std::size_t, DrawBindings>& bindings) -> Result<void>;
    [[nodiscard]] auto prepareDraw(std::size_t frameIndex, const MeshDraw& draw) -> Result<DrawBindings>;
    [[nodiscard]] auto prepareProceduralDraw(std::size_t frameIndex, const ProceduralDraw& draw)
        -> Result<DrawBindings>;
    [[nodiscard]] auto prepareProgram(std::size_t frameIndex,
                                      const std::shared_ptr<const ShaderProgram::State>& program,
                                      std::span<const ShaderResourceBinding> resources) -> Result<DrawBindings>;
    [[nodiscard]] auto bindResources(std::size_t frameIndex,
                                     const ShaderProgram::State& program,
                                     std::span<const ShaderResourceBinding> resources,
                                     const ProgramLayout& layout) const -> Result<DrawBindings>;
    [[nodiscard]] auto bindSets(std::size_t frameIndex,
                                const ShaderProgramLayout& reflection,
                                std::span<const ShaderResourceBinding> resources,
                                const ProgramLayout& layout) const -> Result<std::vector<BoundSet>>;
    [[nodiscard]] auto resolveResources(std::size_t frameIndex,
                                        const ShaderProgramLayout& reflection,
                                        std::uint32_t setIndex,
                                        std::span<const ShaderResourceBinding> resources) const
        -> Result<std::vector<BoundResource>>;
    [[nodiscard]] auto prepareSet(const SetLayout& layout,
                                  std::uint32_t index,
                                  std::vector<BoundResource> resources) const -> Result<BoundSet>;
    [[nodiscard]] static auto makeDescriptorWrites(vk::DescriptorSet set,
                                                   std::span<const vk::DescriptorSetLayoutBinding> bindings,
                                                   std::span<const BoundResource> resources)
        -> std::vector<vk::WriteDescriptorSet>;
    [[nodiscard]] auto createResource(std::size_t frameIndex,
                                      const ShaderProgramDescriptor& binding,
                                      const ShaderResourceBinding& resource) const -> Result<BoundResource>;
    [[nodiscard]] auto createUniform(const ShaderProgramDescriptor& binding, const ShaderUniformBlock& value) const
        -> Result<BoundResource>;
    [[nodiscard]] auto resolveStorage(const ShaderProgramDescriptor& binding, const ShaderBufferRange& value) const
        -> Result<BoundResource>;
    [[nodiscard]] auto resolveTexture(std::size_t frameIndex,
                                      const ShaderProgramDescriptor& binding,
                                      const Texture& value) const -> Result<BoundResource>;

    const vk::raii::Device& _device;
    FrameBindings& _frameBindings;
    resources::MemoryPool& _pool;
    std::map<std::uint64_t, ProgramLayout> _layouts;
    std::vector<std::map<std::size_t, DrawBindings>> _frames;
};
}

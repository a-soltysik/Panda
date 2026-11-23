#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <panda/base/Error.hpp>
#include <vector>

#include "renderer/DescriptorSets.hpp"

namespace panda::core::vulkan::renderer
{
struct ShaderProgramLayout;
}

namespace panda::core::vulkan::presentation
{
struct FrameOutput;

class ToneMappingPass final
{
public:
    static constexpr auto frameCount = std::size_t {2};

    [[nodiscard]] static auto create(const vk::raii::Device& device, std::uint32_t maxBoundDescriptorSets)
        -> std::expected<std::unique_ptr<ToneMappingPass>, Error>;

    ToneMappingPass(const ToneMappingPass&) = delete;
    auto operator=(const ToneMappingPass&) -> ToneMappingPass& = delete;
    ToneMappingPass(ToneMappingPass&&) = delete;
    auto operator=(ToneMappingPass&&) -> ToneMappingPass& = delete;
    ~ToneMappingPass();

    [[nodiscard]] auto setInputViews(const std::array<vk::ImageView, frameCount>& views) -> std::expected<void, Error>;
    [[nodiscard]] auto prepare(vk::Format outputFormat) -> std::expected<void, Error>;
    void record(vk::CommandBuffer command, std::size_t frameIndex, const FrameOutput& output) const;

private:
    ToneMappingPass(const vk::raii::Device& device, std::uint32_t maxBoundDescriptorSets);

    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeSampler() -> std::expected<void, Error>;
    [[nodiscard]] auto reflectInterface() -> Result<void>;
    [[nodiscard]] auto reflectDescriptor(const renderer::ShaderProgramLayout& program) -> Result<void>;
    [[nodiscard]] auto createSetLayout() -> Result<void>;
    [[nodiscard]] auto createPipelineLayout() -> Result<void>;
    [[nodiscard]] auto initializeLayouts() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptors() -> std::expected<void, Error>;
    [[nodiscard]] auto createPipeline(vk::Format outputFormat) const -> std::expected<vk::raii::Pipeline, Error>;

    const vk::raii::Device& _device;
    std::uint32_t _maxBoundDescriptorSets;
    std::uint32_t _descriptorSetIndex {};
    std::uint32_t _outputSrgbSpecializationId {};
    vk::raii::Sampler _sampler {nullptr};
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    std::vector<vk::raii::DescriptorSetLayout> _emptyDescriptorSetLayouts;
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    vk::DescriptorSetLayoutBinding _binding;
    renderer::DescriptorSets _descriptors;
    std::map<vk::Format, vk::raii::Pipeline> _pipelines;
};
}

#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <panda/base/Error.hpp>
#include <vector>

namespace panda::core::vulkan::presentation
{
struct FrameOutput;

class ToneMappingPass final
{
public:
    static constexpr auto frameCount = std::size_t {2};

    [[nodiscard]] static auto create(const vk::raii::Device& device)
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
    explicit ToneMappingPass(const vk::raii::Device& device);

    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeSampler() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeLayouts() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeDescriptors() -> std::expected<void, Error>;
    [[nodiscard]] auto createPipeline(vk::Format outputFormat) const -> std::expected<vk::raii::Pipeline, Error>;

    const vk::raii::Device& _device;
    vk::raii::Sampler _sampler {nullptr};
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    vk::raii::DescriptorPool _descriptorPool {nullptr};
    std::vector<vk::raii::DescriptorSet> _descriptorSets;
    std::map<vk::Format, vk::raii::Pipeline> _pipelines;
};
}

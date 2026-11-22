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
#include <panda/base/Error.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>
#include <panda/resources/Mesh.hpp>
#include <panda/resources/Texture.hpp>
#include <vector>

#include "SubmissionQueue.hpp"

namespace panda::detail
{
class PerformanceManagerImpl;
class OffscreenTargetResource;
}

namespace panda::core::vulkan::resources
{
class MemoryPool;
}

namespace panda::core::vulkan::renderer
{
class MeshRenderer;
}

namespace panda::core::vulkan::presentation
{
class ToneMappingPass;

struct FrameOutput
{
    vk::ImageView imageView;
    vk::Extent2D extent;
    vk::Format colorFormat;
};

struct FrameExecutionCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    std::uint32_t queueFamily;
    ::panda::detail::SubmissionQueue& submissions;
    resources::MemoryPool* pool {nullptr};
    ::panda::detail::PerformanceManagerImpl* performance {nullptr};
    std::shared_ptr<TextureFilteringPreset> textureFilteringPreset;
    std::uint32_t maxLights;
    /// First query slot in the shared PerformanceManager array; separate executors need disjoint ranges.
    std::size_t performanceSlotBase {0};
};

class FrameExecution final
{
public:
    static constexpr auto slotCount = std::size_t {2};

    [[nodiscard]] static auto create(const FrameExecutionCreateInfo& info)
        -> std::expected<std::unique_ptr<FrameExecution>, Error>;

    FrameExecution(const FrameExecution&) = delete;
    auto operator=(const FrameExecution&) -> FrameExecution& = delete;
    FrameExecution(FrameExecution&&) = delete;
    auto operator=(FrameExecution&&) -> FrameExecution& = delete;
    ~FrameExecution();

    [[nodiscard]] auto prepareOutput(vk::Extent2D extent,
                                     vk::Format colorFormat,
                                     const RenderView& view,
                                     std::optional<vk::Format> additionalOutputFormat = {}) -> Result<void>;
    [[nodiscard]] auto prepareSlot(std::size_t index,
                                   const RenderView& view,
                                   std::shared_ptr<::panda::detail::OffscreenTargetResource> target = {})
        -> std::expected<void, Error>;
    [[nodiscard]] auto beginCommands(std::size_t index) -> std::expected<void, Error>;
    [[nodiscard]] auto recordContent(std::size_t index,
                                     const FrameOutput& output,
                                     const RenderView& view,
                                     const FrameOutput* additionalOutput = nullptr) -> std::expected<void, Error>;
    [[nodiscard]] auto endCommands(std::size_t index) -> std::expected<void, Error>;
    [[nodiscard]] auto command(std::size_t index) const -> vk::CommandBuffer;
    void setLastCompletion(std::size_t index, std::uint64_t value);
    void markSubmitted(std::size_t index, std::uint64_t value, bool hadRendererWork);
    void discardUnsubmittedInputs(std::size_t index);
    [[nodiscard]] auto drain() -> std::expected<void, Error>;
    [[nodiscard]] auto isGpuTimestampSamplingEnabled() const noexcept -> bool;
    [[nodiscard]] auto getSampleCount(const RenderView& view) const noexcept -> SampleCount;
    [[nodiscard]] auto getEffectiveAnisotropy(const RenderView& view) const noexcept -> float;

private:
    struct ExecutionSlot
    {
        vk::raii::CommandPool pool {nullptr};
        vk::CommandBuffer command;
        std::uint64_t lastCompletion {0};
        std::vector<Mesh> retainedMeshes;
        std::vector<Texture> retainedTextures;
        std::shared_ptr<::panda::detail::OffscreenTargetResource> retainedTarget;
    };

    explicit FrameExecution(const FrameExecutionCreateInfo& info);

    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeRenderer(vk::Extent2D extent, vk::Format colorFormat, SampleCount requested)
        -> Result<void>;
    [[nodiscard]] auto prepareRenderer(vk::Extent2D extent, SampleCount requested) -> Result<void>;
    [[nodiscard]] auto resizeRenderer(vk::Extent2D extent, SampleCount requested) -> Result<void>;
    [[nodiscard]] auto initializeToneMapping() -> Result<void>;
    [[nodiscard]] auto setToneMappingInputViews() -> Result<void>;
    [[nodiscard]] auto prepareToneMappingOutputs(vk::Format colorFormat,
                                                 std::optional<vk::Format> additionalOutputFormat) -> Result<void>;
    [[nodiscard]] static auto retainViewResources(ExecutionSlot& slot,
                                                  const RenderView& view,
                                                  std::shared_ptr<::panda::detail::OffscreenTargetResource> target)
        -> std::expected<void, Error>;
    [[nodiscard]] auto recordViewContent(std::size_t index,
                                         const FrameOutput& output,
                                         const RenderView& view,
                                         const FrameOutput* additionalOutput) -> std::expected<void, Error>;
    [[nodiscard]] auto recordRendererView(std::size_t index,
                                          const FrameOutput& output,
                                          const RenderView& view,
                                          const FrameOutput* additionalOutput) -> std::expected<void, Error>;
    void recordToneMappingOutputs(const ExecutionSlot& slot,
                                  std::size_t index,
                                  const FrameOutput& output,
                                  const FrameOutput* additionalOutput) const;
    [[nodiscard]] auto waitCompletion(std::uint64_t value) const -> std::expected<void, Error>;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    std::uint32_t _queueFamily;
    ::panda::detail::SubmissionQueue& _submissions;
    resources::MemoryPool* _pool;
    ::panda::detail::PerformanceManagerImpl* _performance;
    std::shared_ptr<TextureFilteringPreset> _textureFilteringPreset;
    std::uint32_t _maxLights;
    std::size_t _performanceSlotBase;
    std::unique_ptr<renderer::MeshRenderer> _renderer;
    std::unique_ptr<ToneMappingPass> _toneMapping;
    std::array<ExecutionSlot, slotCount> _slots;
};
}

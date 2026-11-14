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
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/FrameResult.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/RenderView.hpp>

#include "SubmissionQueue.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/FrameExecution.hpp"
#include "resources/OffscreenTargetResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
class BufferResource;
}

namespace panda::core::vulkan::presentation
{
struct OffscreenRenderingCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    std::uint32_t queueFamily;
    ::panda::detail::SubmissionQueue& submissions;
    resources::MemoryPool& pool;
    ::panda::detail::PerformanceManagerImpl* performance {nullptr};
    std::uint32_t maxLights;
};

struct WindowCaptureOutput
{
    vk::Image image;
    vk::Extent2D extent;
    vk::Format format;
    bool initialized {false};
};

struct WindowCaptureRequest
{
    const RenderView& view;
    const OffscreenTarget& target;
    WindowCaptureOutput output;
    vk::Semaphore acquire;
    vk::Semaphore finished;
    std::uint64_t& submittedValue;
    std::optional<std::size_t>& submittedPerformanceSlot;
};

class OffscreenRendering final
{
public:
    [[nodiscard]] static auto create(const OffscreenRenderingCreateInfo& info)
        -> std::expected<std::unique_ptr<OffscreenRendering>, Error>;

    OffscreenRendering(const OffscreenRendering&) = delete;
    auto operator=(const OffscreenRendering&) -> OffscreenRendering& = delete;
    OffscreenRendering(OffscreenRendering&&) = delete;
    auto operator=(OffscreenRendering&&) -> OffscreenRendering& = delete;
    ~OffscreenRendering();

    [[nodiscard]] auto render(const RenderView& view,
                              const OffscreenTarget& target,
                              std::optional<std::size_t>& submittedPerformanceSlot)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto renderWithCapture(const WindowCaptureRequest& request) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto readPixels(const OffscreenTarget& target, CompletionPoint completion)
        -> std::expected<ImageReadback, Error>;
    [[nodiscard]] auto drain() -> std::expected<void, Error>;

private:
    struct FrameRequest
    {
        const RenderView& view;
        const OffscreenTarget& target;
        std::optional<std::size_t>& submittedPerformanceSlot;
        const WindowCaptureOutput* windowOutput {nullptr};
        vk::Semaphore acquire {};
        vk::Semaphore finished {};
        std::uint64_t* submittedValue {nullptr};
    };

    struct PendingReadback
    {
        vk::raii::CommandPool commandPool {nullptr};
        vk::CommandBuffer command {};
        std::shared_ptr<::panda::detail::BufferResource> buffer;
        std::shared_ptr<::panda::detail::OffscreenTargetResource> target;
        vk::Extent2D extent {};
        std::uint64_t completion {0};
    };

    explicit OffscreenRendering(const OffscreenRenderingCreateInfo& info);
    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto prepareRendererOutput(const ::panda::detail::OffscreenTargetResource& target,
                                             const RenderView& view) -> std::expected<void, Error>;
    [[nodiscard]] auto recordFrame(std::size_t slotIndex,
                                   const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                   const FrameRequest& request) -> std::expected<void, Error>;
    [[nodiscard]] auto renderInternal(const FrameRequest& request) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto submitFrame(std::size_t slotIndex,
                                   const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                   const FrameRequest& request) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto submitReadback(const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target)
        -> std::expected<ImageReadback, Error>;
    [[nodiscard]] auto submitReadbackCommands(const std::shared_ptr<::panda::detail::OffscreenTargetResource>& target,
                                              std::shared_ptr<::panda::detail::BufferResource> buffer,
                                              vk::Extent2D extent) -> std::expected<void, Error>;
    [[nodiscard]] auto waitPendingReadback() -> std::expected<void, Error>;
    [[nodiscard]] auto collectPendingReadback() -> std::expected<ImageReadback, Error>;
    [[nodiscard]] static auto copyReadbackBytes(const ::panda::detail::BufferResource& buffer, vk::Extent2D extent)
        -> std::expected<ImageReadback, Error>;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    std::uint32_t _queueFamily;
    ::panda::detail::SubmissionQueue& _submissions;
    resources::MemoryPool& _pool;
    ::panda::detail::PerformanceManagerImpl* _performance;
    std::uint32_t _maxLights;
    std::unique_ptr<FrameExecution> _execution;
    std::optional<vk::Extent2D> _rendererExtent;
    vk::Format _rendererFormat {vk::Format::eUndefined};
    std::optional<PendingReadback> _pendingReadback;
    std::size_t _nextSlot {0};
};
}

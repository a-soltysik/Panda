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
#include <panda/Context.hpp>
#include <panda/WindowSurface.hpp>

#include "SubmissionQueue.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/FrameExecution.hpp"
#include "presentation/SwapchainGeneration.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::core::vulkan
{
namespace api_detail = ::panda::detail;

struct PresentationCreateInfo
{
    vk::PhysicalDevice physicalDevice;
    const vk::raii::Device& device;
    vk::SurfaceKHR surface;
    const WindowSurface& window;
    ContextDeviceInfo deviceInfo;
    api_detail::SubmissionQueue& submissions;
    resources::MemoryPool* pool {nullptr};
    api_detail::PerformanceManagerImpl* performance {nullptr};
};

class Presentation final
{
public:
    [[nodiscard]] static auto create(const PresentationCreateInfo& info)
        -> std::expected<std::unique_ptr<Presentation>, Error>;

    Presentation(const Presentation&) = delete;
    auto operator=(const Presentation&) -> Presentation& = delete;
    Presentation(Presentation&&) = delete;
    auto operator=(Presentation&&) -> Presentation& = delete;
    ~Presentation();

    [[nodiscard]] auto render(const RenderView& view) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto render(const RenderView& view, std::optional<std::size_t>& submittedPerformanceSlot)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto drain() -> std::expected<void, Error>;

private:
    struct AcquiredImage
    {
        enum class Status : std::uint8_t
        {
            Ready,
            Skipped,
            RecreatePending
        };

        Status status {Status::Skipped};
        std::uint32_t index {0};
        bool suboptimal {false};
    };

    struct FrameSelection
    {
        std::size_t slotIndex {0};
        std::uint32_t imageIndex {0};
    };

    struct PendingImageRecovery
    {
        std::uint32_t index;
        bool replaceRenderFinished;
        bool releaseFailed {false};
    };

    explicit Presentation(const PresentationCreateInfo& info);

    [[nodiscard]] auto initialize(const PresentationCreateInfo& info) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameResources(const PresentationCreateInfo& info) -> std::expected<void, Error>;
    [[nodiscard]] auto initializeAcquireSemaphores() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeWindowPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto verifyReleaseFunction() const -> std::expected<void, Error>;
    [[nodiscard]] auto initializeAcquireSemaphore(vk::raii::Semaphore& semaphore) const -> std::expected<void, Error>;
    [[nodiscard]] auto recreate(FramebufferExtent requested) -> std::expected<void, Error>;
    [[nodiscard]] auto recordFrame(std::size_t slotIndex, const SwapchainImage& image, const RenderView& view)
        -> std::expected<void, Error>;
    [[nodiscard]] auto waitCompletion(std::uint64_t value) const -> std::expected<void, Error>;
    [[nodiscard]] auto waitPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto activeGeneration() -> SwapchainGeneration&;
    [[nodiscard]] auto activeGeneration() const -> const SwapchainGeneration&;
    [[nodiscard]] auto effectiveSampleCount() const noexcept -> vk::SampleCountFlagBits;
    [[nodiscard]] auto ensureWindowGeneration(FramebufferExtent requested) -> std::expected<void, Error>;
    [[nodiscard]] auto prepareWindowFrame() -> std::expected<bool, Error>;
    [[nodiscard]] auto nonReadyFrameResult(AcquiredImage::Status status) const -> std::optional<FrameResult>;
    [[nodiscard]] auto executeAcquiredFrame(std::size_t slotIndex,
                                            const RenderView& view,
                                            std::optional<std::size_t>& submittedPerformanceSlot)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto submitAcquiredFrame(const AcquiredImage& acquired,
                                           std::size_t slotIndex,
                                           const RenderView& view,
                                           std::optional<std::size_t>& submittedPerformanceSlot)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto acquireFrame(std::size_t slotIndex) -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto interpretAcquiredImage(vk::Result result, std::uint32_t index)
        -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto waitImage(std::size_t slotIndex, std::uint32_t index) -> std::expected<void, Error>;
    [[nodiscard]] auto submitRecordedFrame(std::size_t slotIndex, const SwapchainImage& image) -> Result<std::uint64_t>;
    [[nodiscard]] auto submitFrame(std::size_t slotIndex,
                                   SwapchainImage& image,
                                   FrameSelection selection,
                                   const RenderView& view) -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto abortFrame(std::size_t slotIndex, std::uint32_t imageIndex, const Error& cause)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto handleSubmitFailure(FrameSelection selection, const Error& cause)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto presentFrame(SwapchainImage& image,
                                    std::uint32_t index,
                                    std::uint64_t completion,
                                    bool acquireSuboptimal) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto queuePresent(const SwapchainImage& image, std::uint32_t index) -> vk::Result;
    [[nodiscard]] auto recoverPresentFailure(std::uint32_t index, vk::Result result, std::uint64_t completion)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto finishPendingImageRecovery() -> std::expected<void, Error>;
    [[nodiscard]] auto replacePendingRenderFinishedSemaphore() -> std::expected<void, Error>;
    [[nodiscard]] auto releaseImage(std::uint32_t index) const -> std::expected<void, Error>;
    [[nodiscard]] auto abortAcquire(FrameSelection selection) -> std::expected<void, Error>;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    vk::raii::Queue _presentQueue;
    vk::SurfaceKHR _surface {};
    const WindowSurface& _window;
    ContextDeviceInfo _deviceInfo;
    api_detail::SubmissionQueue& _submissions;
    std::unique_ptr<presentation::FrameExecution> _execution;
    std::array<vk::raii::Semaphore, presentation::FrameExecution::slotCount> _acquireSemaphores {
        vk::raii::Semaphore {nullptr}, vk::raii::Semaphore {nullptr}};
    std::optional<SwapchainGeneration> _generation;
    std::size_t _nextSlot {0};
    bool _recreatePending {false};
    bool _faulted {false};
    bool _acquireRetirementFailed {false};
    std::optional<PendingImageRecovery> _pendingImageRecovery;
};

}

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
#include "presentation/SwapchainGeneration.hpp"

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

    [[nodiscard]] auto presentClearFrame() -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto drain() -> std::expected<void, Error>;

private:
    struct FrameSlot
    {
        vk::raii::CommandPool pool {nullptr};
        vk::CommandBuffer command;
        vk::raii::Semaphore acquire {nullptr};
        std::uint64_t lastCompletion {0};
    };

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

    explicit Presentation(const PresentationCreateInfo& info);

    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameResources() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeWindowPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto verifyReleaseFunction() const -> std::expected<void, Error>;
    [[nodiscard]] auto initializeSlot(FrameSlot& slot) const -> std::expected<void, Error>;
    [[nodiscard]] auto recreate(FramebufferExtent requested) -> std::expected<void, Error>;
    [[nodiscard]] auto recordClear(FrameSlot& slot, const SwapchainImage& image, std::size_t slotIndex)
        -> std::expected<void, Error>;
    [[nodiscard]] static auto recordFrameCommands(FrameSlot& slot) -> std::expected<void, Error>;
    [[nodiscard]] auto recordFrameWork(FrameSlot& slot, const SwapchainImage& image, std::size_t slotIndex)
        -> std::expected<void, Error>;
    [[nodiscard]] auto waitCompletion(std::uint64_t value) const -> std::expected<void, Error>;
    [[nodiscard]] auto waitPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto activeGeneration() -> SwapchainGeneration&;
    [[nodiscard]] auto activeGeneration() const -> const SwapchainGeneration&;
    [[nodiscard]] auto ensureWindowGeneration(FramebufferExtent requested) -> std::expected<void, Error>;
    [[nodiscard]] auto prepareWindowFrame() -> std::expected<bool, Error>;
    [[nodiscard]] static auto nonReadyFrameResult(AcquiredImage::Status status) -> std::optional<FrameResult>;
    [[nodiscard]] auto executeAcquiredFrame() -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto submitAcquiredFrame(FrameSlot& slot, const AcquiredImage& acquired, std::size_t slotIndex)
        -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto acquireFrame(FrameSlot& slot) -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto interpretAcquiredImage(vk::Result result, std::uint32_t index)
        -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto waitImage(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>;
    [[nodiscard]] auto submitRecordedFrame(const FrameSlot& slot, const SwapchainImage& image) -> Result<std::uint64_t>;
    [[nodiscard]] auto submitFrame(FrameSlot& slot, SwapchainImage& image, FrameSelection selection)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto abortFrame(FrameSlot& slot, std::uint32_t imageIndex, const Error& cause)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto handleSubmitFailure(FrameSlot& slot, FrameSelection selection, const Error& cause)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto presentFrame(SwapchainImage& image,
                                    std::uint32_t index,
                                    std::uint64_t completion,
                                    bool acquireSuboptimal) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto queuePresent(const SwapchainImage& image, std::uint32_t index) -> vk::Result;
    [[nodiscard]] auto recoverPresentFailure(SwapchainImage& image,
                                             std::uint32_t index,
                                             vk::Result result,
                                             std::uint64_t completion) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto releaseImage(std::uint32_t index) const -> std::expected<void, Error>;
    [[nodiscard]] auto abortAcquire(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>;

    vk::PhysicalDevice _physicalDevice;
    const vk::raii::Device& _device;
    vk::raii::Queue _presentQueue;
    vk::SurfaceKHR _surface {};
    const WindowSurface& _window;
    ContextDeviceInfo _deviceInfo;
    api_detail::SubmissionQueue& _submissions;
    api_detail::PerformanceManagerImpl* _performance;
    std::array<FrameSlot, 2> _slots;
    std::optional<SwapchainGeneration> _generation;
    std::size_t _nextSlot {0};
    bool _recreatePending {false};
    bool _faulted {false};
    bool _acquireRetirementFailed {false};
};

}

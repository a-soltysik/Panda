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

#include "SwapchainGeneration.hpp"

namespace panda::detail
{

class Presentation final
{
public:
    [[nodiscard]] static auto create(vk::PhysicalDevice physicalDevice,
                                     vk::Device device,
                                     VkSurfaceKHR surface,
                                     WindowSurface& window,
                                     const ContextDeviceInfo& deviceInfo)
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
        vk::UniqueCommandPool pool;
        vk::CommandBuffer command;
        vk::UniqueSemaphore acquire;
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

    Presentation(vk::PhysicalDevice physicalDevice,
                 vk::Device device,
                 VkSurfaceKHR surface,
                 WindowSurface& window,
                 const ContextDeviceInfo& deviceInfo);

    [[nodiscard]] auto initialize() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeFrameResources() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeWindowPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto resolveReleaseFunction() -> std::expected<void, Error>;
    [[nodiscard]] auto initializeSlot(FrameSlot& slot) const -> std::expected<void, Error>;
    [[nodiscard]] auto recreate(FramebufferExtent requested) -> std::expected<void, Error>;
    [[nodiscard]] auto recordClear(FrameSlot& slot, SwapchainImage& image) -> std::expected<void, Error>;
    [[nodiscard]] auto waitCompletion(std::uint64_t value) const -> std::expected<void, Error>;
    [[nodiscard]] auto waitPresentation() -> std::expected<void, Error>;
    [[nodiscard]] auto activeGeneration() -> SwapchainGeneration&;
    [[nodiscard]] auto activeGeneration() const -> const SwapchainGeneration&;
    [[nodiscard]] auto prepareWindowFrame() -> std::expected<bool, Error>;
    [[nodiscard]] auto executeAcquiredFrame() -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto acquireFrame(FrameSlot& slot) -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto interpretAcquiredImage(vk::Result result, std::uint32_t index)
        -> std::expected<AcquiredImage, Error>;
    [[nodiscard]] auto waitImage(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>;
    [[nodiscard]] auto submitRecordedFrame(FrameSlot& slot, const SwapchainImage& image, std::uint64_t completion)
        -> vk::Result;
    [[nodiscard]] auto submitFrame(FrameSlot& slot, SwapchainImage& image, std::uint32_t index)
        -> std::expected<std::uint64_t, Error>;
    [[nodiscard]] auto presentFrame(SwapchainImage& image,
                                    std::uint32_t index,
                                    std::uint64_t completion,
                                    bool acquireSuboptimal) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto queuePresent(SwapchainImage& image, std::uint32_t index) -> vk::Result;
    [[nodiscard]] auto recoverPresentFailure(SwapchainImage& image,
                                             std::uint32_t index,
                                             vk::Result result,
                                             std::uint64_t completion) -> std::expected<FrameResult, Error>;
    [[nodiscard]] auto releaseImage(std::uint32_t index) const -> std::expected<void, Error>;
    [[nodiscard]] auto abortAcquire(FrameSlot& slot, std::uint32_t index) -> std::expected<void, Error>;

    vk::PhysicalDevice _physicalDevice;
    vk::Device _device;
    vk::Queue _graphicsQueue;
    vk::Queue _presentQueue;
    VkSurfaceKHR _surface {};
    WindowSurface& _window;
    ContextDeviceInfo _deviceInfo;
    vk::UniqueSemaphore _timeline;
    std::array<FrameSlot, 2> _slots;
    std::optional<SwapchainGeneration> _generation;
    PFN_vkReleaseSwapchainImagesKHR _releaseKhr {nullptr};
    PFN_vkReleaseSwapchainImagesEXT _releaseExt {nullptr};
    std::uint64_t _nextCompletion {1};
    std::size_t _nextSlot {0};
    bool _recreatePending {false};
    bool _faulted {false};
};

}

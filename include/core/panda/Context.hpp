#pragma once

/// @file
/// Vulkan context ownership and device capability diagnostics.

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <string>

namespace panda
{
class WindowSurface;

/// @brief Options for creating a Vulkan 1.3 context.
struct ContextOptions
{
    /// Name passed to the Vulkan loader; copied during creation.
    std::string applicationName {"Panda"};
    /// Enable the installed Khronos validation layer and route its debug-utils diagnostics
    /// to the Panda logger; failure is reported if the layer or debug-utils extension is absent.
    bool enableValidation {false};
};

/// @brief Selected device and enabled core feature diagnostics.
struct ContextDeviceInfo
{
    /// @brief Enabled swapchain-maintenance extension variant.
    enum class SwapchainMaintenance : std::uint8_t
    {
        None,
        Khr,
        Ext
    };

    /// Vulkan physical-device name.
    std::string name;
    /// Vulkan API version reported by the selected physical device.
    std::uint32_t apiVersion {0};
    /// Graphics and compute queue family selected for ordered work.
    std::uint32_t queueFamily {0};
    /// Present queue family; equals queueFamily when one family supports both.
    std::uint32_t presentQueueFamily {0};
    /// Windowed capability, absent for a context without a surface.
    SwapchainMaintenance swapchainMaintenance {SwapchainMaintenance::None};
};

/// @brief Result of one window-frame attempt.
struct FrameResult
{
    /// @brief Window-frame outcome.
    enum class Status : std::uint8_t
    {
        Presented,
        Skipped,
        Suspended,
        RecreatePending
    };

    /// Outcome; RecreatePending can still carry an already submitted frame.
    Status status {Status::Skipped};
    /// Committed graphics timeline value, or zero when no GPU work was submitted.
    std::uint64_t completionValue {0};
};

/// @brief Move-only owner of a Vulkan instance, device and optional swapchain.
/// Calls on one Context must be serialized. A quiescent Context may move between
/// threads. A borrowed WindowSurface and its native window must outlive this Context;
/// windowed calls follow that surface's thread requirements.
class Context final
{
public:
    /// @brief Creates a context without a window surface and selects a Vulkan 1.3
    /// graphics/compute device with dynamic rendering, synchronization2 and timeline semaphores enabled.
    /// @return An owner or a capability/backend error. May allocate and block.
    [[nodiscard]] static auto create(const ContextOptions& options = {}) -> Result<Context>;

    /// @brief Creates a context with a window surface using only platform operations from surface.
    /// @param surface Borrowed window-system implementation; it must outlive Context.
    /// @param options Context configuration.
    /// @return An owner or a capability/backend/window-system error. May allocate and block.
    [[nodiscard]] static auto createWithSurface(WindowSurface& surface, const ContextOptions& options = {})
        -> Result<Context>;

    Context(const Context&) = delete;

    auto operator=(const Context&) -> Context& = delete;

    /// @brief Transfers ownership when no operation on this Context is in progress.
    Context(Context&& other) noexcept;

    auto operator=(Context&&) -> Context& = delete;

    /// @brief Drains known presentation work and releases owned Vulkan objects.
    ~Context() noexcept;

    /// @brief Reports ownership; safe on a moved-from object.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Copies selected device diagnostics.
    /// @return Device information. Moved-from use is a fatal contract error.
    [[nodiscard]] auto getDeviceInfo() const -> ContextDeviceInfo;

    /// @brief Clears and presents one diagnostic frame, or reports why it was skipped.
    /// @return Checked frame result or a backend/window failure. A nonzero
    /// completionValue means submitted work, not completed presentation.
    /// This temporary smoke path is replaced by scene rendering in the frame task.
    [[nodiscard]] auto presentClearFrame() const -> Result<FrameResult>;

private:
    struct InstanceServices;
    class Impl;

    [[nodiscard]] static auto createInternal(WindowSurface* surface, const ContextOptions& options) -> Result<Context>;

    explicit Context(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> _implementation;
};

}

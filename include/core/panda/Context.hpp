#pragma once

/// @file
/// Vulkan context ownership and device capability diagnostics.

#include <chrono>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
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
    /// Create timestamp query pools and collect paired CPU and GPU frame timings when the selected queue supports them.
    /// Disabled by default to avoid measurement work in contexts that do not need it.
    bool enableFramePerformanceSampling {false};
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
    /// Vulkan physical-device driver name.
    std::string driverName;
    /// Driver-provided human-readable description and version.
    std::string driverInfo;
    /// Driver version as reported by Vulkan; its encoding is vendor-specific.
    std::uint32_t driverVersion {0};
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
    /// Committed work; empty when no GPU work was submitted. Does not mean presentation completed.
    CompletionPoint completion;
};

/// @brief Move-only owner of a Vulkan instance, device and optional swapchain.
/// Calls on one Context must be serialized. A quiescent Context may move between
/// threads. A borrowed WindowSurface and its native window must outlive this Context;
/// windowed calls follow that surface's thread requirements.
/// Context outlives every Buffer, Texture and Mesh owner created from it, including copied handles.
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
    [[nodiscard]] static auto createWithSurface(const WindowSurface& surface, const ContextOptions& options = {})
        -> Result<Context>;

    Context(const Context&) = delete;

    auto operator=(const Context&) -> Context& = delete;

    /// @brief Transfers ownership when no operation on this Context is in progress.
    Context(Context&& other) noexcept;

    auto operator=(Context&&) -> Context& = delete;

    /// @brief Rejects live upload batches, drains submitted uses/presentation, then requires Buffer, Texture and Mesh owners released.
    /// An ownership violation is fatal.
    ~Context() noexcept;

    /// @brief Reports ownership; safe on a moved-from object.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Copies selected device diagnostics.
    /// @return Device information. Moved-from use is a fatal contract error.
    [[nodiscard]] auto getDeviceInfo() const -> ContextDeviceInfo;

    /// @brief Borrows the Context-owned performance data manager.
    /// @return A manager that exposes device capabilities and ready per-frame CPU/GPU samples;
    /// it must not outlive this Context. Calls remain serialized with Context operations.
    [[nodiscard]] auto getPerformanceManager() -> PerformanceManager&;

    /// @brief Creates an ordinary fixed-size buffer with undefined initial bytes.
    /// @return A shared resource owner or a validation/allocation/backend error.
    /// Context outlives all copies. May allocate; submits no work and does not wait.
    [[nodiscard]] auto createBuffer(const BufferDesc& description) const -> Result<Buffer>;

    /// @brief Creates an optimal-tiled, single-mip, single-layer 2D image with undefined initial contents.
    /// @return A shared image owner or a validation/allocation/backend error.
    /// Context outlives all copies. May allocate; submits no work and does not wait.
    [[nodiscard]] auto createTexture(const TextureDesc& description) const -> Result<Texture>;

    /// @brief Creates fixed vertex storage and optional index storage for one mesh.
    /// @return A shared mesh owner or a validation/allocation/backend error.
    /// Context outlives all copies. May allocate; submits no work and does not wait.
    [[nodiscard]] auto createMesh(const MeshDesc& description) const -> Result<Mesh>;

    /// @brief Creates an empty batch borrowing this Context; may allocate.
    /// @return A batch or an initialization/backend error. Submits no work and performs no completion wait.
    /// Unsubmitted batches must be destroyed before Context; successful submit
    /// consumes the batch and transfers GPU-use retention to Context.
    [[nodiscard]] auto createUploadBatch() const -> Result<UploadBatch>;

    /// @brief Queries GPU completion without waiting; does not indicate display/presentation completion.
    /// @return Completion state or a backend error; empty/foreign tokens are InvalidArgument.
    [[nodiscard]] auto poll(CompletionPoint point) const -> Result<bool>;

    /// @brief Explicitly waits for GPU work; timeout is in nanoseconds and must be nonnegative.
    /// @return Completed, Timeout, or a backend error; rejects empty/foreign tokens.
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>;

    /// @brief Clears and presents one diagnostic frame, or reports why it was skipped.
    /// @return Checked frame result or a backend/window failure. A valid
    /// completion token means submitted work, not completed presentation.
    /// This temporary smoke path is replaced by scene rendering in the frame task.
    [[nodiscard]] auto presentClearFrame() const -> Result<FrameResult>;

private:
    struct InstanceServices;
    class Impl;

    [[nodiscard]] static auto createInternal(const WindowSurface* surface, const ContextOptions& options)
        -> Result<Context>;

    explicit Context(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> _implementation;
    PerformanceManager _performanceManager;
};

}

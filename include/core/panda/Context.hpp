#pragma once

/// @file
/// Vulkan context ownership and device capability diagnostics.

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBufferManager.hpp>
#include <panda/FrameResult.hpp>
#include <panda/Mesh.hpp>
#include <panda/OffscreenRenderer.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/PerformanceManager.hpp>
#include <panda/RenderView.hpp>
#include <panda/ResourceManager.hpp>
#include <panda/SubmissionManager.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/WindowRenderer.hpp>
#include <string>
#include <vulkan/vulkan.hpp>

namespace panda
{
struct Geometry;
class WindowSurface;

/// @brief Options for creating a Vulkan 1.3 context.
struct ContextOptions
{
    /// Default maximum number of lights accepted by the built-in renderer.
    static constexpr std::uint32_t defaultMaxLights {64};

    /// Whether external Vulkan buffers are unavailable, enabled when supported,
    /// or required on the selected device.
    enum class ExternalBufferSupport : std::uint8_t
    {
        Disabled,
        Optional,
        Required
    };

    /// Name passed to the Vulkan loader; copied during creation.
    std::string applicationName {"Panda"};
    /// Enable the installed Khronos validation layer and route its debug-utils diagnostics
    /// to the Panda logger; failure is reported if the layer or debug-utils extension is absent.
    bool enableValidation {false};
    /// Create timestamp query pools and collect paired CPU and GPU frame timings when the selected queue supports them.
    /// Disabled by default to avoid measurement work in contexts that do not need it.
    bool enableFramePerformanceSampling {false};
    /// Maximum lights in one RenderView; zero allows views without direct lighting.
    /// Positive limits must fit the selected device's storage-buffer limits.
    std::uint32_t maxLights {defaultMaxLights};
    /// External Vulkan memory export support.
    ExternalBufferSupport externalBufferSupport {ExternalBufferSupport::Disabled};
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
    /// Stable physical-device identity for compatible external API selection.
    VulkanDeviceUuid deviceUuid {};
    /// True when external buffer memory export was enabled for this Context.
    /// Individual buffer usages are still checked when an external buffer is created.
    bool externalBufferExportEnabled {false};
    /// True when this Context can export opaque timeline semaphore handles for external buffers.
    bool externalSemaphoreExportEnabled {false};
    /// Vulkan API version reported by the selected physical device.
    std::uint32_t apiVersion {0};
    /// Graphics and compute queue family selected for ordered work.
    std::uint32_t queueFamily {0};
    /// Present queue family; equals queueFamily when one family supports both.
    std::uint32_t presentQueueFamily {0};
    /// Windowed capability, absent for a context without a surface.
    SwapchainMaintenance swapchainMaintenance {SwapchainMaintenance::None};
};

/// @brief Move-only owner of a Vulkan instance, device and optional swapchain.
/// Calls on one Context must be serialized. A quiescent Context may move between
/// threads. A borrowed WindowSurface and its native window must outlive this Context;
/// windowed calls follow that surface's thread requirements.
/// Context outlives every Buffer, Texture, Mesh and OffscreenTarget owner created from it, including copied handles.
class Context final
{
public:
    /// @brief Creates a context without a window surface and selects a Vulkan 1.3
    /// graphics/compute device with dynamic rendering, synchronization2 and timeline semaphores enabled.
    /// @param options Application name, optional validation/frame sampling, and the light budget.
    /// @return A fully initialized owner, or an Error. `Unsupported` means the
    /// loader/device lacks required capabilities or cannot satisfy the light budget;
    /// select a compatible runtime/device or lower `maxLights`.
    /// If requested validation support is missing, install it or disable
    /// `ContextOptions::enableValidation`. `ResourceExhausted` means other resources
    /// may need to be released before retrying.
    /// The error category is a value of `ErrorCode`; other native failures preserve
    /// their cause in `Error::message`, optional `Error::native` and `Error::source`.
    /// Inspect the category before choosing a recovery action.
    /// No Context is returned on failure. May allocate and block.
    /// @see Error for diagnostic fields.
    /// @see ErrorCode for recovery categories.
    [[nodiscard]] static auto create(const ContextOptions& options = {}) -> Result<Context>;

    /// @brief Creates a context with a window surface using only platform operations from surface.
    /// @param surface Borrowed window-system implementation; its extension list must
    /// include `VK_KHR_surface` and the platform surface extension. It and its native
    /// window must outlive Context; calls follow its thread requirements.
    /// @param options Application name, optional validation/frame sampling, and the light budget.
    /// @return A fully initialized owner, or an Error. `Unsupported` means required
    /// surface/device capabilities or extensions are unavailable, including a device
    /// unable to satisfy the light budget; use a compatible runtime/device, lower
    /// `maxLights`, or disable validation if its optional support is missing.
    /// `InvalidArgument` can mean the surface extension list omits `VK_KHR_surface`;
    /// include it and the platform-required extensions. Surface-provider errors,
    /// including `SurfaceLost`, are propagated; recover or recreate the platform
    /// surface before creating another Context. Provider errors can use any
    /// `ErrorCode`.
    /// `ResourceExhausted` means other resources may need to be released before retrying.
    /// Other native failures preserve their cause in `Error::message`, optional
    /// `Error::native` and `Error::source`. Inspect the category before choosing a
    /// recovery action.
    /// No Context is returned on failure. May allocate and block.
    /// @see Error for diagnostic fields.
    /// @see ErrorCode for recovery categories.
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

    /// @brief Borrows this Context's resource creation operations.
    /// @return A manager that must not outlive this Context.
    [[nodiscard]] auto getResourceManager() const -> const ResourceManager&;

    /// @brief Borrows this Context's upload and completion operations.
    /// @return A manager that must not outlive this Context.
    [[nodiscard]] auto getSubmissionManager() const -> const SubmissionManager&;

    /// @brief Borrows this Context's offscreen renderer.
    /// @return A renderer that can target any compatible OffscreenTarget from this Context.
    [[nodiscard]] auto getOffscreenRenderer() const -> const OffscreenRenderer&;

    /// @brief Borrows this Context's window renderer when it has a window surface.
    /// @return A renderer reference, or an empty optional for a headless Context.
    [[nodiscard]] auto getWindowRenderer() const -> std::optional<std::reference_wrapper<const WindowRenderer>>;

    /// @brief Borrows the Context-owned performance data manager.
    /// @return A manager that exposes device capabilities and ready per-frame CPU/GPU samples;
    /// it must not outlive this Context. Calls remain serialized with Context operations.
    [[nodiscard]] auto getPerformanceManager() -> PerformanceManager&;

    /// @brief Borrows this Context's external-buffer creation and handoff operations.
    [[nodiscard]] auto getExternalBufferManager() -> ExternalBufferManager&;

private:
    friend class ResourceManager;
    friend class SubmissionManager;
    friend class OffscreenRenderer;
    friend class WindowRenderer;

    class Impl;

    [[nodiscard]] auto createBuffer(const BufferDesc& description) const -> Result<Buffer>;
    [[nodiscard]] auto createTexture(const TextureDesc& description) const -> Result<Texture>;
    [[nodiscard]] auto createOffscreenTarget(const OffscreenTargetDesc& description) const -> Result<OffscreenTarget>;
    [[nodiscard]] auto createMesh(const MeshDesc& description) const -> Result<Mesh>;
    [[nodiscard]] auto createMeshFromGeometry(const Geometry& geometry) const -> Result<Mesh>;
    [[nodiscard]] auto createUploadBatch() const -> Result<UploadBatch>;
    [[nodiscard]] auto poll(CompletionPoint point) const -> Result<bool>;
    [[nodiscard]] auto wait(CompletionPoint point, std::chrono::nanoseconds timeout) const -> Result<WaitStatus>;
    [[nodiscard]] auto renderWindow(const RenderView& view) const -> Result<FrameResult>;
    [[nodiscard]] auto renderWindowWithCapture(const RenderView& view, const OffscreenTarget& target) const
        -> Result<FrameResult>;
    [[nodiscard]] auto renderOffscreen(const RenderView& view, const OffscreenTarget& target) const
        -> Result<FrameResult>;
    [[nodiscard]] auto readPixels(const OffscreenTarget& target, CompletionPoint completion) const
        -> Result<ImageReadback>;

    [[nodiscard]] static auto createInternal(const WindowSurface* surface, const ContextOptions& options)
        -> Result<Context>;
    explicit Context(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> _implementation;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    OffscreenRenderer _offscreenRenderer;
    WindowRenderer _windowRenderer;
    PerformanceManager _performanceManager;
    ExternalBufferManager _externalBufferManager;
};

}

#pragma once

/// @file
/// Vulkan context ownership and device capability diagnostics.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/DeviceInfo.hpp>
#include <panda/EngineOptions.hpp>
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

namespace panda
{
struct Geometry;
class WindowSurface;

/// @brief Move-only owner of a Vulkan instance, device and optional swapchain.
/// Calls on one Engine must be serialized. A quiescent Engine may move between
/// threads. A borrowed WindowSurface and its native window must outlive this Engine;
/// windowed calls follow that surface's thread requirements.
/// Engine outlives every Buffer, Texture, Mesh and OffscreenTarget owner created from it, including copied handles.
class Engine final
{
public:
    /// @brief Creates a context without a window surface and selects a Vulkan 1.3
    /// graphics/compute device with dynamic rendering, synchronization2 and timeline semaphores enabled.
    /// @param options Application name, optional validation/frame sampling, and the light budget.
    /// @return A fully initialized owner, or an Error. `Unsupported` means the
    /// loader/device lacks required capabilities or cannot satisfy the light budget;
    /// select a compatible runtime/device or lower `maxLights`.
    /// If requested validation support is missing, install it or disable
    /// `EngineOptions::enableValidation`. `ResourceExhausted` means other resources
    /// may need to be released before retrying.
    /// The error category is a value of `ErrorCode`; other native failures preserve
    /// their cause in `Error::message`, optional `Error::native` and `Error::source`.
    /// Inspect the category before choosing a recovery action.
    /// No Engine is returned on failure. May allocate and block.
    /// @see Error for diagnostic fields.
    /// @see ErrorCode for recovery categories.
    [[nodiscard]] static auto create(const EngineOptions& options = {}) -> Result<Engine>;

    /// @brief Creates a context with a window surface using only platform operations from surface.
    /// @param surface Borrowed window-system implementation; its extension list must
    /// include `VK_KHR_surface` and the platform surface extension. It and its native
    /// window must outlive Engine; calls follow its thread requirements.
    /// @param options Application name, optional validation/frame sampling, and the light budget.
    /// @return A fully initialized owner, or an Error. `Unsupported` means required
    /// surface/device capabilities or extensions are unavailable, including a device
    /// unable to satisfy the light budget; use a compatible runtime/device, lower
    /// `maxLights`, or disable validation if its optional support is missing.
    /// `InvalidArgument` can mean the surface extension list omits `VK_KHR_surface`;
    /// include it and the platform-required extensions. Surface-provider errors,
    /// including `SurfaceLost`, are propagated; recover or recreate the platform
    /// surface before creating another Engine. Provider errors can use any
    /// `ErrorCode`.
    /// `ResourceExhausted` means other resources may need to be released before retrying.
    /// Other native failures preserve their cause in `Error::message`, optional
    /// `Error::native` and `Error::source`. Inspect the category before choosing a
    /// recovery action.
    /// No Engine is returned on failure. May allocate and block.
    /// @see Error for diagnostic fields.
    /// @see ErrorCode for recovery categories.
    [[nodiscard]] static auto createWithSurface(const WindowSurface& surface, const EngineOptions& options = {})
        -> Result<Engine>;

    Engine(const Engine&) = delete;

    auto operator=(const Engine&) -> Engine& = delete;

    /// @brief Transfers ownership when no operation on this Engine is in progress.
    Engine(Engine&& other) noexcept;

    auto operator=(Engine&&) -> Engine& = delete;

    /// @brief Rejects live upload batches, drains submitted uses/presentation, then requires Buffer, Texture and Mesh owners released.
    /// An ownership violation is fatal.
    ~Engine() noexcept;

    /// @brief Reports ownership; safe on a moved-from object.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Copies selected device diagnostics.
    /// @return Device information. Moved-from use is a fatal contract error.
    [[nodiscard]] auto getDeviceInfo() const -> DeviceInfo;

    /// @brief Borrows this Engine's resource creation operations.
    /// @return A manager that must not outlive this Engine.
    [[nodiscard]] auto getResourceManager() const -> const ResourceManager&;

    /// @brief Borrows this Engine's upload and completion operations.
    /// @return A manager that must not outlive this Engine.
    [[nodiscard]] auto getSubmissionManager() const -> const SubmissionManager&;

    /// @brief Borrows this Engine's offscreen renderer.
    /// @return A renderer that can target any compatible OffscreenTarget from this Engine.
    [[nodiscard]] auto getOffscreenRenderer() const -> const OffscreenRenderer&;

    /// @brief Borrows this Engine's window renderer when it has a window surface.
    /// @return A renderer reference, or an empty optional for a headless Engine.
    [[nodiscard]] auto getWindowRenderer() const -> std::optional<std::reference_wrapper<const WindowRenderer>>;

    /// @brief Borrows the Engine-owned performance data manager.
    /// @return A manager that exposes device capabilities and ready per-frame CPU/GPU samples;
    /// it must not outlive this Engine. Calls remain serialized with Engine operations.
    [[nodiscard]] auto getPerformanceManager() -> PerformanceManager&;

    /// @brief Borrows this Engine's external-buffer creation and handoff operations.
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

    [[nodiscard]] static auto createInternal(const WindowSurface* surface, const EngineOptions& options)
        -> Result<Engine>;
    explicit Engine(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> _implementation;
    ResourceManager _resourceManager;
    SubmissionManager _submissionManager;
    OffscreenRenderer _offscreenRenderer;
    WindowRenderer _windowRenderer;
    PerformanceManager _performanceManager;
    ExternalBufferManager _externalBufferManager;
};

}

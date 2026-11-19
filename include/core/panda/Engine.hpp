#pragma once

/// @file
/// Common Vulkan engine API and device capability diagnostics.

#include <memory>
#include <optional>
#include <panda/DeviceInfo.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/base/Error.hpp>
#include <panda/interop/ExternalBufferManager.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/PerformanceManager.hpp>
#include <panda/rendering/WindowRenderer.hpp>
#include <panda/resources/ResourceManager.hpp>
#include <panda/submission/SubmissionManager.hpp>

namespace panda
{
namespace core::vulkan
{
struct EngineCreateInfo;
}

class WindowSurface;

/// @brief Effective optional facilities of a successfully created Engine.
struct EngineFeatures
{
    /// Whether this Engine owns a window renderer.
    bool windowRenderingEnabled {false};
    /// Whether Vulkan validation was enabled for this Engine.
    bool validationEnabled {false};
    /// GPU timestamp support and whether frame sampling is active.
    PerformanceCapabilities performance;
    /// Effective window presentation mode; absent for headless Engines.
    std::optional<EngineOptions::PresentationMode> presentationMode;
    /// Whether external buffer memory export was enabled.
    bool externalBufferExportEnabled {false};
    /// Whether external timeline semaphore export was enabled.
    bool externalSemaphoreExportEnabled {false};
};

/// @brief Move-only owner of a Vulkan device and its services.
/// Calls on one Engine must be serialized. A quiescent Engine may move between
/// threads. A borrowed WindowSurface and its native window must outlive a
/// window-enabled Engine; window calls follow that surface's thread requirements.
/// Borrowed service references keep their address when Engine moves, but must not
/// outlive the moved-to Engine. Engine outlives every Buffer, Texture, Mesh and
/// OffscreenTarget owner created from it, including copied handles.
class Engine final
{
public:
    /// @brief Creates a headless Vulkan Engine with common services.
    /// @return A fully initialized owner, or an Error. Creation may allocate and block.
    [[nodiscard]] static auto create(const EngineOptions& options = {}) -> Result<Engine>;

    /// @brief Creates an Engine with a Vulkan window surface.
    /// @param surface Borrowed surface provider and native window; both must outlive the Engine.
    /// @param options Creation options applied to the Engine.
    /// @return A fully initialized owner with window rendering enabled, or an Error.
    /// Surface, device and presentation errors are returned without publishing a partial owner.
    /// Creation may allocate and block.
    [[nodiscard]] static auto createWithSurface(const WindowSurface& surface, const EngineOptions& options = {})
        -> Result<Engine>;

    Engine(const Engine&) = delete;
    auto operator=(const Engine&) -> Engine& = delete;
    /// @brief Transfers ownership from another Engine.
    /// @param other Engine whose implementation is transferred.
    Engine(Engine&& other) noexcept;
    auto operator=(Engine&&) -> Engine& = delete;
    ~Engine() noexcept;

    /// @brief Reports ownership; safe on a moved-from object.
    [[nodiscard]] auto isValid() const noexcept -> bool;

    /// @brief Copies selected device diagnostics.
    /// @return Device information. Moved-from use is a fatal contract error.
    [[nodiscard]] auto getDeviceInfo() const -> DeviceInfo;

    /// @brief Copies the optional-feature snapshot negotiated during creation.
    /// @return Effective availability for this Engine. Moved-from use is a fatal contract error.
    [[nodiscard]] auto getFeatures() const -> EngineFeatures;

    /// @brief Borrows this Engine's resource creation operations.
    /// @return A manager that must not outlive Engine. Its address remains stable across moves.
    [[nodiscard]] auto getResourceManager() const -> const ResourceManager&;

    /// @brief Borrows this Engine's upload and completion operations.
    /// @return A manager that must not outlive Engine. Its address remains stable across moves.
    [[nodiscard]] auto getSubmissionManager() const -> const SubmissionManager&;

    /// @brief Borrows this Engine's offscreen renderer.
    /// @return A renderer that can target any compatible OffscreenTarget from this Engine.
    /// Its address remains stable when Engine moves; moved-from access is a fatal contract error.
    [[nodiscard]] auto getOffscreenRenderer() const -> const OffscreenRenderer&;

    /// @brief Borrows the active performance-sampling manager.
    /// @return A manager that exposes ready per-frame CPU/GPU samples. Access is a fatal
    /// contract error unless timestamp sampling is enabled; inspect getFeatures() first.
    /// Its address remains stable when Engine moves and it must not outlive Engine.
    [[nodiscard]] auto getPerformanceManager() -> PerformanceManager&;

    /// @brief Borrows the window renderer.
    /// @return A renderer that remains valid while Engine owns it. Access is a fatal
    /// contract error if Engine was created without a window surface.
    [[nodiscard]] auto getWindowRenderer() const -> const WindowRenderer&;

    /// @brief Borrows this Engine's external-buffer creation and handoff operations.
    /// @return A manager whose address remains stable when Engine moves; it must not
    /// outlive Engine. Unsupported operations return Unsupported.
    [[nodiscard]] auto getExternalBufferManager() -> ExternalBufferManager&;

private:
    class Impl;

    explicit Engine(core::vulkan::EngineCreateInfo&& resources);
    [[nodiscard]] static auto createInternal(const WindowSurface* surface, const EngineOptions& options)
        -> Result<Engine>;

    std::unique_ptr<Impl> _implementation;
};
}
